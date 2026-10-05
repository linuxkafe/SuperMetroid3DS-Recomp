/*
 * DS-adapted Super Metroid frame driver.
 *
 * This is the LLE path of SuperMetroidRecomp/src/sm_rtl.c, rehosted on the
 * DS with no fibers and no generated code:
 *
 *   - The game runs as one linear program. Reset ($80:841C) falls into the
 *     main loop, which waits at WaitForNMI ($80:8338 / $80:8343).
 *   - Every host frame we run the guest forward from the last resume PC.
 *     Frame 0 boots at the reset vector; later frames push a hardware NMI
 *     frame and execute the ARCHITECTURAL NMI through interp_bridge
 *     (interp_bridge_run_interrupt at $80:9583, the NMI vector) instead of
 *     the desktop generated I_NMI body.
 *   - The raster pass (SmDrawPpuFrame) runs HDMA + line render and, at the
 *     programmed H/V-timer scanline, pushes an IRQ frame and executes
 *     $80:986A via interp_bridge_run_interrupt for the HUD/room split.
 *
 * Diagnostics that only exist as host observability (SM_LLE_RESUME_DIAG,
 * SM_DMA_TRACE, SM_RASTER_PROBE) are omitted; the improved-renderer capture
 * (SmRendererCaptureLine) is omitted because g_sm_video.enhanced and
 * g_sm_video.fps_enabled are both false on DS (see ds_rtl_support.c), so it
 * can never fire.
 */
#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "snes/snes.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include "ds_rtl.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Saved architectural register file across the NMI run (same shape the
 * desktop fiber path keeps; NMI talks to the main line through RAM). */
typedef struct SmCpuSave {
  uint16_t A, X, Y, S, D;
  uint8_t  DB, PB, P;
  uint8_t  host_return_valid;
  uint8_t  m_flag, x_flag, emulation;
  uint8_t  _flag_N, _flag_V, _flag_Z, _flag_C, _flag_I, _flag_D;
  CpuTailcallContextSave tailcall_context;
} SmCpuSave;

static void sm_save_cpu(SmCpuSave *s, const CpuState *c) {
  s->A = c->A; s->X = c->X; s->Y = c->Y; s->S = c->S; s->D = c->D;
  s->DB = c->DB; s->PB = c->PB; s->P = c->P;
  s->host_return_valid = c->host_return_valid;
  s->m_flag = c->m_flag; s->x_flag = c->x_flag; s->emulation = c->emulation;
  s->_flag_N = c->_flag_N; s->_flag_V = c->_flag_V; s->_flag_Z = c->_flag_Z;
  s->_flag_C = c->_flag_C; s->_flag_I = c->_flag_I; s->_flag_D = c->_flag_D;
  cpu_tailcall_context_save(&s->tailcall_context);
}
static void sm_restore_cpu(CpuState *c, const SmCpuSave *s) {
  c->A = s->A; c->X = s->X; c->Y = s->Y; c->S = s->S; c->D = s->D;
  c->DB = s->DB; c->PB = s->PB; c->P = s->P;
  c->host_return_valid = s->host_return_valid;
  c->m_flag = s->m_flag; c->x_flag = s->x_flag; c->emulation = s->emulation;
  c->_flag_N = s->_flag_N; c->_flag_V = s->_flag_V; c->_flag_Z = s->_flag_Z;
  c->_flag_C = s->_flag_C; c->_flag_I = s->_flag_I; c->_flag_D = s->_flag_D;
  cpu_tailcall_context_restore(&s->tailcall_context);
}

uint16_t counter_global_frames;

static bool g_game_started = false;
static bool g_game_done = false;
static uint32_t g_lle_resume_pc = 0x808343u;

/* Vectors from recomp/bank00.cfg: NMI=$80:9583, IRQ=$80:986A. */
#define SM_VECTOR_NMI 0x809583u
#define SM_VECTOR_IRQ 0x80986Au

void RunOneFrameOfGame(void) {
  /* Door-loader routines wait for NMI from inside their bodies; execute
   * their authoritative ROM bytes under the LLE scheduler instead. */
  static const uint32_t k_lle_door_loader_targets[] = {
    0x828948u,  /* Game_RunOneFrameOfGame: LLE-authoritative gameplay */
    0x828B44u,  /* GameState_8_MainGameplay */
    0x82E169u,  /* GameState_9_HitDoorBlock */
    0x82E1B7u,  /* GameState_10_LoadingNextRoom_Async */
    0x82E288u,  /* GameState_11_LoadingNextRoom_Async */
    0x82DFD1u,  /* LoadEnemyGfxToVram */
    0x82E039u,  /* inline-parameter VRAM transfer */
    0x82E4A9u,  /* DoorTransitionFunction_LoadMoreThings_Async */
    0x82E5D9u,  /* door-dependent background transfer dispatcher */
    0x82E5EBu,  /* UpdateBackgroundCommand_2_TransferToVram */
  };
  interp_bridge_set_lle_bounce_exclusions(
      k_lle_door_loader_targets,
      sizeof(k_lle_door_loader_targets) /
          sizeof(k_lle_door_loader_targets[0]));

  /* The real $80:8338 WaitForNMI asserts $05B4, then spins at $80:8343 until
   * NMI clears it. Run reset only on the first host frame; every later frame
   * injects NMI and resumes at that exact guest PC. */
  if (g_game_done)
    return;
  uint32_t entry_pc = g_lle_resume_pc;
  if (!g_game_started) {
    cpu_state_init(&g_cpu, g_ram);
    g_game_started = true;
    entry_pc = 0x80841Cu;
  } else {
    SmCpuSave interrupted;
    sm_save_cpu(&interrupted, &g_cpu);
    g_snes->inNmi = true;
    cpu_push_interrupt_frame(&g_cpu);
    interp_bridge_run_interrupt(&g_cpu, SM_VECTOR_NMI);
    /* The host-injected NMI communicates through SNES memory and hardware
     * state. Restore the interrupted main-line register file exactly as the
     * desktop fiber path does. */
    sm_restore_cpu(&g_cpu, &interrupted);

    /* The synthetic continuation is $80:8343, immediately after WaitForNMI's
     * `SEP #$30`. Enforce the width contract hardware necessarily has at this
     * exact resume PC; otherwise LDA $05B4 becomes a 16-bit read, observes
     * adjacent $05B5, and spins to the interpreter step cap during door
     * transitions. */
    if ((entry_pc & 0xFFFFu) == 0x8343u) {
      g_cpu.P |= 0x30u;
      cpu_p_to_mirrors(&g_cpu);
      g_cpu.X &= 0x00FFu;
      g_cpu.Y &= 0x00FFu;
      g_cpu.DB = (uint8_t)(entry_pc >> 16);
    }
  }

  if (!interp_bridge_run_loop(&g_cpu, entry_pc, 0x808343u, 0x05B4u, 1)) {
    g_game_done = true;
  } else {
    uint32_t next_pc = interp_bridge_lle_resume_pc();
    if (next_pc)
      g_lle_resume_pc = next_pc;
  }
  ++counter_global_frames;
}

void SmDrawPpuFrame(void) {
  SimpleHdma hdma_chans[8];
  Dma *dma = g_dma;

  /* This loop IS the HDMA engine for the frame: it walks the real tables per
   * line. The beam-timeline HDMA must stay off, or every table is consumed
   * twice. Set every frame: a save-state load restores this Snes field. */
  snes_set_hdma_beam_enabled(g_snes, false);

  /* Reinitialize HDMA from the last $420C (HDMAEN) value written during NMI.
   * Super Metroid drives the HUD/status split and various color/window
   * effects through HDMA. */
  dma_startDma(dma, g_snesrecomp_last_hdmaen, true);

  /* HDMA objects are not confined to channels 5-7 (the Ceres elevator uses
   * channel 3 for the BGMODE split and channel 2 for color math). Process
   * every enabled hardware channel in priority order. */
  for (int ch = 0; ch < 8; ch++)
    SimpleHdma_Init(&hdma_chans[ch], &dma->channel[ch]);

  /* Super Metroid programs H/V IRQ for the HUD/minimap raster split
   * (IrqHandler_*_BeginHud/EndHud at $80:986A). Latch the timer IRQ at the
   * programmed scanline so the split runs mid-frame. */
  int trigger = g_snes->vIrqEnabled ? g_snes->vTimer : -1;

  for (int i = 0; i <= 224; i++) {
    /* HDMA runs during the H-blank preceding each visible scanline. The
     * raster IRQ then selects the register set for that line. */
    for (int ch = 0; ch < 8; ch++)
      SimpleHdma_DoLine(&hdma_chans[ch]);
    if (i == trigger) {
      g_snes->inIrq = true;
      cpu_push_interrupt_frame(&g_cpu);
      interp_bridge_run_interrupt(&g_cpu, SM_VECTOR_IRQ);
      trigger = g_snes->vIrqEnabled ? g_snes->vTimer : -1;
    }
    ppu_runLine(g_ppu, i);
  }
}