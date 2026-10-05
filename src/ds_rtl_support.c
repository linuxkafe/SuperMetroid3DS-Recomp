/*
 * DS runtime support — the small, real subset of common_rtl.c the DS
 * interpreter-first build links.
 *
 * The desktop runtime (common_rtl.c) is a ~2,500-line host today: frame
 * clock / audio recovery, netplay, run-ahead, mod-audio, trace files. The DS
 * build does not compile it. Instead this file provides the exact globals
 * and functions the LLE core (cpu_state, the snes/ tree, common_cpu_infra,
 * the 65816 interp bridge) actually references, with bodies ported verbatim from
 * common_rtl.c where the behavior is architectural.
 *
 * Nothing here invents results: g_ram / pacing anchors / the memsel+hdmaen
 * shadows are plain storage the core reads and writes; RtlApuLock/Unlock are
 * no-ops for the same reason the framework's *headless* build makes them
 * no-ops (this port pumps the SPC synchronously on ARM9, no audio thread);
 * g_ws_active stays false because this port has no guest widescreen
 * (SuperMetroidRecomp: "the guest stays at 256 pixels"). The one runtime
 * knob dropped is SNESRECOMP_APU_TOUCH_CYCLES (host observability; default
 * scale is 1.0, unchanged).
 */
#include "common_rtl.h"
#include "apu_frame_clock.h"
#include "common_cpu_infra.h"
#include "cpu_state.h"
#include "snes/snes.h"
#include "snes/cart.h"

#include <stdbool.h>

/* ── guest memory + machine identity (common_rtl.c:47,50) ─────────────── */
uint8 g_ram[0x20000];
const uint8 *g_rom;

/* ── APU pacing anchors (common_rtl.c:168-184,210) ────────────────────── */
uint64_t g_main_cpu_cycles_estimate = 0;
uint64_t g_apu_pace_cycles_estimate = 0;
uint64_t g_apu_last_sync_cycles = 0;
uint64_t g_apu_last_sync_master = 0;
int g_interp_apu_driving = 0; /* the interp bridge sets 1 around LLE exec   */

/* Memsel / last-HDMAEN shadows read by the raster and memory paths. */
uint8 g_snesrecomp_last_hdmaen = 0;
uint8_t g_memsel = 0;

/* Per-game widescreen knobs (widescreen.c NOTE: storage is per-game). */
bool g_ws_active = false;
uint8 g_ws_extra = 0;

/* Frame-clock state rtl_reset_host_pacing() must reset (common_rtl.c:215). */
static uint64_t g_apu_frame_start_master;
static RtlApuFrameClock g_apu_frame_clock;
static bool g_apu_frame_time_valid;

/* Single-threaded ARM9 main loop: the SPC is pumped synchronously by the
 * interpreter, so these real mutexes are correctly no-ops here. */
void RtlApuLock(void) {}
void RtlApuUnlock(void) {}

/* The DS port starts no mod-audio stream; stopping one is a no-op. */
void snes_mod_audio_stop_all(void) {}

/* Stubs for debug functions (desktop observability features). */
void debug_on_wram_write_byte(uint32_t addr, uint8_t old_val, uint8_t val) { (void)addr; (void)old_val; (void)val; }
void debug_on_wram_write_word(uint32_t addr, uint16_t old_val, uint16_t val) { (void)addr; (void)old_val; (void)val; }
void debug_on_block_enter(uint32_t pc, uint32_t a, uint32_t x, uint32_t y) { (void)pc; (void)a; (void)x; (void)y; }

/* Stub for g_spc_player (SPC player not used on DS). */
struct SpcPlayer *g_spc_player = NULL;

/* Stub for snes_mod_audio_mix (mod audio feature not on DS). */
void snes_mod_audio_mix(int16_t *dst, int frame_count, uint32_t output_rate, uint32_t output_channels) {
    (void)dst; (void)frame_count; (void)output_rate; (void)output_channels;
}

/* Stub for snes_netplay_active (netplay feature not on DS). Returns 0 (no netplay). */
int snes_netplay_active(void) { return 0; }

/* Stubs for blkDev functions (block device layer for libfat DVM).
 * These map to the default DLDI disc interface set up by fatInitDefault(). */
#include <nds/disc_io.h>

extern const DISC_INTERFACE default_disc_interface;

/* Default disc interface implementation using DLDI functions.
 * This will be patched by the DLDI loader at runtime. */
const DISC_INTERFACE default_disc_interface = {
    .ioType = 0,
    .features = FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE,
    .startup = NULL,
    .isInserted = NULL,
    .readSectors = NULL,
    .writeSectors = NULL,
    .clearStatus = NULL,
    .shutdown = NULL
};

bool blkInit(void) { return true; }
bool blkDevInit(void) { return true; }
bool blkDevIsPresent(void) { return true; }
sec_t blkDevGetSectorCount(void) { return 0; }
bool blkDevReadSectors(sec_t sector, sec_t num_sectors, void* buffer) {
    return default_disc_interface.readSectors(sector, num_sectors, buffer);
}
bool blkDevWriteSectors(sec_t sector, sec_t num_sectors, const void* buffer) {
    return default_disc_interface.writeSectors(sector, num_sectors, buffer);
}

/* Stub for g_isTwlMode (DSi mode detection). */
int g_isTwlMode = 0;

/* rtl_accumulate_apu_catchup — ported from common_rtl.c:1582, default path.
 * While the interp bridge drives (g_interp_apu_driving=1) it returns
 * immediately: the bridge advances the SPC per interpreted opcode by guest
 * master cycles, and a synthetic per-touch credit would double-count. */
void rtl_accumulate_apu_catchup(void) {
  if (g_interp_apu_driving) return;
  uint64_t delta = g_apu_pace_cycles_estimate - g_apu_last_sync_cycles;
  g_apu_last_sync_cycles = g_apu_pace_cycles_estimate;
  g_snes->apuCatchupCycles += (double)delta * 2.0 / 7.0;
}

/* rtl_reset_host_pacing — ported from common_rtl.c, the cold-boot anchors
 * SnesInit() requires (common_cpu_infra.c:1096). */
void rtl_reset_host_pacing(void) {
  snes_frame_counter = 0;
  g_apu_frame_time_valid = false;
  g_apu_frame_clock = (RtlApuFrameClock){0};
  g_apu_frame_start_master = g_cpu.master_cycles;
  g_main_cpu_cycles_estimate = 0;
  g_apu_pace_cycles_estimate = 0;
  g_apu_last_sync_cycles = 0;
  g_apu_last_sync_master = g_cpu.master_cycles;
  snes_mod_audio_stop_all();
  snes_refresh_state_set(0u, g_cpu.master_cycles);
}

/* ── SimpleHdma: the host HDMA table walk (common_rtl.c:2383-2501) ──────
 * Ported verbatim. Table pointers resolve through the exported RomPtr
 * (ds_rtl_regs.c), which is where this file's private ds_rom_ptr() went: the
 * runner core needs RomPtr by that name anyway, and two copies of one address
 * map would be two maps to keep in sync. */

static const uint8 *SimpleHdma_GetPtr(uint32 p) {
  return RomPtr(p);
}

static bool SimpleHdma_PtrRangeValid(const uint8 *p, size_t length) {
  uintptr_t addr = (uintptr_t)p;
  uintptr_t ram_base = (uintptr_t)g_ram;
  if (addr >= ram_base) {
    size_t offset = (size_t)(addr - ram_base);
    if (offset <= sizeof(g_ram) && length <= sizeof(g_ram) - offset)
      return true;
  }
  uint32 rom_size = g_snes && g_snes->cart
                  ? (uint32)g_snes->cart->romSize : 0;
  uintptr_t rom_base = (uintptr_t)g_rom;
  if (g_rom && rom_size != 0 && addr >= rom_base) {
    size_t offset = (size_t)(addr - rom_base);
    if (offset <= rom_size && length <= (size_t)rom_size - offset)
      return true;
  }
  return false;
}

void SimpleHdma_Init(SimpleHdma *c, DmaChannel *dc) {
  /* Calling this IS the declaration that the host walks the HDMA tables in
   * its own raster loop, so the beam must not walk them twice per line. */
  if (g_snes) snes_set_hdma_beam_enabled(g_snes, false);
  if (!dc->hdmaActive) {
    c->table = 0;
    return;
  }
  c->table = SimpleHdma_GetPtr(dc->aAdr | dc->aBank << 16);
  c->rep_count = 0;
  c->mode = dc->mode | dc->indirect << 6;
  c->ppu_addr = dc->bAdr;
  c->indir_bank = dc->indBank;
}

/* Stub for host_report_breadcrumb (desktop observability feature). */
void host_report_breadcrumb(const char *fmt, ...) {
    (void)fmt;
}

void SimpleHdma_DoLine(SimpleHdma *c) {
  static const uint8 bAdrOffsets[8][4] = {
    {0, 0, 0, 0},
    {0, 1, 0, 1},
    {0, 0, 0, 0},
    {0, 0, 1, 1},
    {0, 1, 2, 3},
    {0, 1, 0, 1},
    {0, 0, 0, 0},
    {0, 0, 1, 1}
  };
  static const uint8 transferLength[8] = {
    1, 2, 2, 4, 4, 4, 2, 4
  };

  if (c->table == NULL)
    return;
  bool do_transfer = false;
  if ((c->rep_count & 0x7f) == 0) {
    if (!SimpleHdma_PtrRangeValid(c->table, 1)) {
      c->table = NULL;
      return;
    }
    c->rep_count = *c->table++;
    if (c->rep_count == 0) {
      c->table = NULL;
      return;
    }
    if (c->mode & 0x40) {
      if (!SimpleHdma_PtrRangeValid(c->table, 2)) {
        c->table = NULL;
        return;
      }
      c->indir_ptr = SimpleHdma_GetPtr(
          (uint32)c->indir_bank << 16 | c->table[0] | c->table[1] * 256);
      c->table += 2;
    }
    do_transfer = true;
  }
  if (do_transfer || c->rep_count & 0x80) {
    for (int j = 0, j_end = transferLength[c->mode & 7]; j < j_end; j++) {
      const uint8 *src = c->mode & 0x40 ? c->indir_ptr : c->table;
      if (!SimpleHdma_PtrRangeValid(src, 1)) {
        c->table = NULL;
        break;
      }
      uint8 v = *src;
      if (c->mode & 0x40)
        c->indir_ptr++;
      else
        c->table++;
      /* ppu_write takes the B-bus offset ($00-$3F), not a $21xx CPU address. */
      uint8 reg = (uint8)(c->ppu_addr + bAdrOffsets[c->mode & 7][j]);
      ppu_write(g_ppu, reg, v);
      debug_server_on_reg_write((uint16)(0x2100u + reg), v);
    }
  }
  c->rep_count--;
}