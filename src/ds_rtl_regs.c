/*
 * DS B-bus register dispatch — the DS port of the five register accessors in
 * common_rtl.c that the interpreter build actually links: RomPtr, WriteReg,
 * ReadRegOpenBus, ReadReg, ReadRegWord, WriteRegWord.
 *
 * Why this file exists: the runner core routes EVERY guest hardware access
 * through these. cpu_state.c's cpu_write8/16/32 call WriteReg/WriteRegWord,
 * its cpu_read8/16 call ReadReg*, and common_cpu_infra.c's SnesRomPtr/IndirPtr
 * call RomPtr. Without them the LLE interpreter cannot execute a single I/O
 * instruction — this is not optional shim, it is the port's bus.
 *
 * common_rtl.c itself is NOT compiled on DS (see the Makefile RUNNER_CORE note
 * and ds_rtl_support.c): it is the desktop host's ~2,500-line runtime and
 * collides with the DS shim's globals.
 *
 * KNOWN INCOMPLETE — the link is still red after this file. WriteReg's
 * $2140-$217F branch calls RtlApuWrite and ReadRegWord's APU-port path calls
 * rtl_sync_apu_to_cpu_locked / rtl_apu_port_observers_read. Those three are the
 * APU half (T013/T014) and are deliberately NOT stubbed here — see
 * docs/ROADMAP.md [DISCOVERED]. Super Metroid drives $2140 constantly (SPC700
 * IPL upload, echo), so the interpreter cannot boot until T013 lands. The call
 * sites below are real calls to a function that does not exist yet, NOT
 * no-op placeholders: a silent stub here would look like working audio and
 * would hide the missing ticket.
 *
 * Dropped from the desktop versions, and why:
 *   - The getenv("SNESRECOMP_OFFRAILS_STDERR") stderr dump in RomPtr: desktop
 *     stderr diagnostics. The g_fail latch and the cpu_trace_offrails() call
 *     are kept, so an off-rails access trips the same failure state.
 *   - _writereg_ppu_count / _writereg_dma_count: write counters with no reader
 *     in the desktop file either.
 *   - RtlApuLock/RtlApuUnlock bodies: real mutexes on desktop (audio thread).
 *     This port pumps the SPC synchronously on ARM9, so they are correct
 *     no-ops — already defined that way in ds_rtl_support.c, alongside the
 *     framework's own *headless* build making the same call.
 */
#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "cpu_state.h"
#include "cpu_trace.h"
#include "snes/snes.h"
#include "snes/ppu.h"
#include "snes/dma.h"
#include "snes/cart.h"
#include "snes/cx4.h"
#include "snes/apu.h"
#include "snes/msu1.h"
#include "snes/sa1.h"
#include "snes/ws_shadow.h"
#include "recomp_hw.h"

/* ── machine globals the core reads through common_rtl.h ──────────────────
 * common_rtl.c:48-52. Plain storage: SnesInit (common_cpu_infra.c:1066-1067,
 * 1109-1110) is the only writer, assigning them from the Snes it builds. The
 * desktop file is not compiled here, so the definitions must exist. */
uint8 *g_sram;
int g_sram_size;
Ppu *g_ppu;
Dma *g_dma;

/* ── RomPtr — common_rtl.c:1310 ───────────────────────────────────────────
 * DIVERGENCE FROM DESKTOP, deliberate, do not "fix" by copying upstream:
 *
 * desktop RomPtr is ROM-only. It delegates every bank to cart_getRomPtr,
 * which returns NULL for $7E/$7F (cart.c:143) and for the $00-$3F/$80-$BF
 * $0000-$1FFF mirror, then reports off-rails and hands back &g_rom[0].
 * A WRAM address through desktop RomPtr therefore reads ROM byte 0 and latches
 * g_fail.
 *
 * This port resolves WRAM banks and the mirror to g_ram first, falling back
 * to the cart mapping and only then to &g_rom[0]. That is the behavior the DS
 * port already had (ds_rom_ptr, introduced with the HDMA walk in
 * ds_rtl_support.c and covered by SimpleHdma_PtrRangeValid, which validates
 * table pointers against g_ram *or* g_rom — a check that only makes sense if
 * pointers can legitimately land in WRAM). Reverting to desktop's exact
 * mapping would regress that path.
 *
 * The divergence is recorded in docs/ROADMAP.md [DISCOVERED]. It is a
 * faithfulness deviation and belongs on the record, not buried here. */
uint8 *RomPtr(uint32_t addr) {
  uint8_t bank = (uint8_t)(addr >> 16);
  uint16_t lo = (uint16_t)addr;
  if (bank == 0x7E)
    return g_ram + lo;
  if (bank == 0x7F)
    return g_ram + 0x10000 + lo;
  /* Low-RAM mirror: $00-$3F and $80-$BF at $0000-$1FFF alias WRAM bank 0. */
  if ((bank < 0x40 || (bank >= 0x80 && bank < 0xC0)) && lo < 0x2000)
    return g_ram + lo;
  /* Cx4 working RAM reached through a ROM-shaped pointer. NOT reachable from
   * cpu_read8, which intercepts the Cx4 window before its RomPtr fallthrough.
   * cx4_ram_ptr returns NULL for the $7F40-$7F5E MMIO registers, which must go
   * through cart_read and so fall through to the off-rails report below. */
  if (g_snes && cart_is_cx4_window(g_snes->cart, bank, lo)) {
    uint8_t *cx4p = cx4_ram_ptr(g_snes->cart->cx4, lo);
    if (cx4p)
      return cx4p;
  }
  uint8_t *mapped = g_snes && g_snes->cart
                      ? cart_getRomPtr(g_snes->cart, bank, lo)
                      : NULL;
  if (!mapped) {
    if (!g_fail)
      g_fail = true;
    /* No printf — a bad pointer read in a loop floods stderr with millions of
     * identical lines. cpu_trace_offrails is the investigation channel. */
    cpu_trace_offrails("RomPtr-invalid", addr);
  }
  /* Resolve mapping and actual-size mirroring through Cart, so a
   * valid-looking guest address cannot become an out-of-allocation host read. */
  return mapped ? mapped : (uint8 *)&g_rom[0];
}

/* ── WriteReg — common_rtl.c:1357 ──────────────────────────────────────────
 * Ported verbatim. The leading snes_sync_master_clock is load-bearing and is
 * NOT a DS-specific addition: AOT-compiled code never advances the PPU beam,
 * so the desktop host syncs on every hardware touch to keep compiled and
 * interpreted code on one clock. It is omitted there only because there is no
 * AOT code on DS — the interpreter calls snes_sync_master_clock after every
 * opcode instead, and the same asymmetry this comment describes (a beam that
 * jumps in one lump stepping over an IRQ window test, losing the raster split
 * entirely rather than late) applies unchanged. */
void WriteReg(uint16 reg, uint8 value) {
  if (g_snes)
    snes_sync_master_clock(g_snes, g_cpu.master_cycles);
  /* Direct dispatch — bypass emulator bus.
   * MSU-1 ($2000-$2007). Inert unless a pack is armed, so the open-bus
   * default (no-op + log) is preserved byte-for-byte when disabled. */
  if (g_snes && cart_has_sa1(g_snes->cart) &&
      ((reg >= 0x2200 && reg < 0x2400) || (reg >= 0x3000 && reg < 0x3800))) {
    cart_sync_coprocessors(g_snes->cart, g_cpu.master_cycles);
    cart_write(g_snes->cart, 0, reg, value);
    debug_server_on_reg_write(reg, value);
    return;
  }
  if (reg >= 0x2000 && reg < 0x2008) {
    if (msu1_enabled())
      msu1_write(reg, value);
    debug_server_on_reg_write(reg, value);
    return;
  }
  if (reg >= 0x3000 && reg < 0x3300 && g_snes->cart->type == CART_SUPERFX) {
    /* AOT code reaches the GSU through this direct-register fast path. Keep the
     * coprocessor on the CPU's current master clock before either side observes
     * or changes its registers. */
    cart_sync_coprocessors(g_snes->cart, g_cpu.master_cycles);
    cart_write(g_snes->cart, 0, reg, value);
  } else if (reg >= 0x2100 && reg < 0x2140) {
    ppu_write(g_ppu, reg & 0xff, value);
    if (g_snes)
      ppu_rasterRecord(reg, g_snes->vPos, value);
  } else if (reg >= 0x2140 && reg < 0x2180) {
    /* T013/T014 — not yet implemented on DS. Deliberately a real call to a
     * missing symbol so the gap is a link error, never a silent drop. */
    RtlApuWrite(reg, value);
  } else if (reg >= 0x2180 && reg < 0x2184) {
    snes_writeBBus(g_snes, reg & 0xff, value);
  } else if (reg >= 0x4200 && reg < 0x4220) {
    if (reg == 0x420C) {
      g_snesrecomp_last_hdmaen = value;
      /* Per-line fact: Super Metroid switches the transition's HDMA channels
       * on from the line-21 raster handler, and a frame-model host that
       * samples the mask once at render time never sees them.
       * See raster_reg_journaled() in ppu.c. */
      if (g_snes)
        ppu_rasterRecord(reg, g_snes->vPos, value);
    }
    if (reg == 0x420D)
      g_memsel = (uint8_t)(value & 1); /* FastROM select; paces $80-FF code */
    recomp_write_internal_reg(reg, value);
  } else if (reg >= 0x4300 && reg < 0x4380) {
    dma_write(g_dma, reg, value);
  } else if (reg >= 0x4800 && reg < 0x4808 && g_snes && g_snes->cart &&
             g_snes->cart->type == CART_SDD1) {
    /* S-DD1 decompression-chip registers. The LLE interpreter's cpu_write8
     * funnels everything in $2000-$5FFF through WriteReg; without this case
     * the game's enables and MMC selects were silently dropped. */
    cart_sync_coprocessors(g_snes->cart, g_cpu.master_cycles);
    cart_write(g_snes->cart, 0, reg, value);
  }
  debug_server_on_reg_write(reg, value);
}

/* ── ReadRegOpenBus — common_rtl.c:1447 ───────────────────────────────────
 * Ported verbatim. */
uint8 ReadRegOpenBus(uint16 reg, uint8 open_bus) {
  /* Direct dispatch — bypass emulator bus.
   * MSU-1 ($2000-$2007). An absent device leaves the data bus undriven. */
  if (g_snes && cart_has_sa1(g_snes->cart) &&
      ((reg >= 0x2200 && reg < 0x2400) || (reg >= 0x3000 && reg < 0x3800))) {
    cart_sync_coprocessors(g_snes->cart, g_cpu.master_cycles);
    return sa1_cpu_read(g_snes->cart->sa1, 0, reg, open_bus);
  }
  if (reg >= 0x2000 && reg < 0x2008)
    return msu1_enabled() ? msu1_read(reg) : open_bus;
  if (reg == 0x2137)
    snes_sync_master_clock(g_snes, g_cpu.master_cycles);
  if (reg >= 0x3000 && reg < 0x3300 && g_snes->cart->type == CART_SUPERFX) {
    cart_sync_coprocessors(g_snes->cart, g_cpu.master_cycles);
    return cart_read(g_snes->cart, 0, reg);
  }
  if (reg >= 0x2100 && reg < 0x2140) {
    return ppu_read(g_ppu, reg & 0xff);
  } else if (reg >= 0x2140 && reg < 0x2180) {
    /* APU read — route through emulator (real SPC700 outPorts). */
    return snes_read(g_snes, reg);
  } else if (reg == 0x2180) {
    return snes_readBBus(g_snes, reg & 0xff);
  } else if (reg == 0x4016 || reg == 0x4017) {
    /* JOYSER0 / JOYSER1 — manual joypad-read serial registers. Routed through
     * snes_readReg so the core returns the controller-presence signature (bit 0
     * set after the implicit "16 reads done" state). Falling through to the
     * default `return open_bus` breaks CheckWhichControllersArePluggedIn. */
    return snes_readReg(g_snes, reg);
  } else if (reg >= 0x4200 && reg < 0x4220) {
    return recomp_read_internal_reg(reg);
  } else if (reg >= 0x4300 && reg < 0x4380) {
    return dma_read(g_dma, reg);
  } else if (reg >= 0x4800 && reg < 0x4808 && g_snes && g_snes->cart &&
             g_snes->cart->type == CART_SDD1) {
    /* S-DD1 register reads: mirror the write path so ioRead (bsnes) semantics
     * apply under the interpreter instead of returning open bus. */
    cart_sync_coprocessors(g_snes->cart, g_cpu.master_cycles);
    return cart_read(g_snes->cart, 0, reg);
  }
  return open_bus;
}

uint8 ReadReg(uint16 reg) {
  return ReadRegOpenBus(reg, g_cpu.open_bus);
}

/* ── ReadRegWord — common_rtl.c:1500 ─────────────────────────────────────
 * Ported verbatim. */
uint16 ReadRegWord(uint16 reg) {
  /* APU port quirk: 16-bit CMP $2140 must see a CONSISTENT outPorts snapshot.
   * Two separate ReadReg calls would each catch the APU up — between them the
   * SPC could write only the LO byte (port 0) before the host has read HI
   * (port 1), so the host sees a torn value. Read both ports atomically
   * (single guest-time sync) for the APU-port range.
   * T013/T014 — the three APU calls below are not yet implemented on DS. */
  if (reg >= 0x2140 && reg <= 0x217F) {
    RtlApuLock();
    rtl_sync_apu_to_cpu_locked();
    uint8_t lo = g_snes->apu->outPorts[(reg & 0x3)];
    uint8_t hi = g_snes->apu->outPorts[((reg + 1) & 0x3)];
    RtlApuUnlock();
    lo = rtl_apu_port_observers_read(reg, lo);
    hi = rtl_apu_port_observers_read(reg + 1, hi);
    g_cpu.open_bus = hi;
    return (uint16_t)lo | ((uint16_t)hi << 8);
  }
  uint8_t lo = ReadReg(reg);
  g_cpu.open_bus = lo;
  uint8_t hi = ReadReg(reg + 1);
  g_cpu.open_bus = hi;
  return (uint16_t)lo | ((uint16_t)hi << 8);
}

static void WriteVramWord(Ppu *ppu, uint16 value) {
  uint16_t adr = ppu->vramPointer;
  ppu->vram[adr & 0x7fff] = value;
  /* Atomic 16-bit STA $2118 hits both VRAM bytes at this word; record each as a
   * byte event so a differ can compare against the oracle's REGISTER_2118 +
   * REGISTER_2119 byte sequence. */
  uint32_t byte_addr = (uint32_t)(adr & 0x7fff) << 1;
  debug_server_on_vram_write(byte_addr, (uint8_t)(value & 0xff));
  debug_server_on_vram_write(byte_addr + 1, (uint8_t)(value >> 8));
  WsShadowOnVramWrite((uint16_t)(adr & 0x7fff), value);
  /* With incOnHigh=1 (VMAIN bit 7) the pointer advances after the high byte
   * ($2119). A 16-bit STA $2118 writes both bytes atomically, so the pointer
   * advances by one word — same as the real hardware path. With incOnHigh=0
   * hardware would advance after the low byte, placing the high byte in the
   * NEXT word, but no sane game does 16-bit STA $2118 in that mode, so the
   * advance is unconditional. */
  ppu->vramPointer += ppu->vramIncrement;
}

/* ── WriteRegWord — common_rtl.c:1546 ────────────────────────────────────
 * Ported verbatim. */
void WriteRegWord(uint16 reg, uint16 value) {
  if (reg == 0x2118) {
    /* VRAM data port: atomic word write. */
    WriteVramWord(g_ppu, value);
    return;
  }
  /* APU port quirk: 16-bit STA $2140 transfers data via $2141 (hi) and the
   * ack-trigger via $2140 (lo). On real hardware both bytes hit the bus
   * together; Super Metroid's SPC IPL upload protocol reads $2141 the moment it
   * sees $2140 change. Writing lo first would let the IPL latch a stale $2141.
   * Order hi-then-lo so $2141 is in place before $2140 fires the trigger. */
  if (reg >= 0x2140 && reg <= 0x217F) {
    WriteReg(reg + 1, value >> 8);
    WriteReg(reg, (uint8)value);
    return;
  }
  WriteReg(reg, (uint8)value);
  WriteReg(reg + 1, value >> 8);
}
