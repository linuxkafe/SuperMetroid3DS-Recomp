/*
 * DS game registration — the DS analog of SuperMetroidRecomp/sm_cpu_infra.c.
 *
 * The desktop game info wires title hooks, native-widescreen and rollback
 * support; the DS port needs only the frame hooks the runner core includes
 * (run_frame / draw_ppu_frame) plus the identity SnesInit() asserts on. It is
 * registered with RtlRegisterGame() before SnesInit() — common_cpu_infra.c
 * hard-asserts that order for every boot path.
 *
 * The runner's SnesInit() runs the framework's own cpu_state_init/snes_reset;
 * the per-game initialize hook is optional by design (NULL-safe) and the DS
 * boot path uses it as NULL: the LLE driver owns its own CpuState warm-up at
 * reset time (ds_rtl.c). All rollback / enhanced-render / save-extra hooks
 * stay NULL because the DS build does not link those subsystems.
 */
#include "common_cpu_infra.h"
#include "ds_rtl.h"

RtlGameInfo kSuperMetroidGameInfo = {
    .title = "Super Metroid",
    .initialize = NULL,
    .run_frame = RunOneFrameOfGame,
    .draw_ppu_frame = SmDrawPpuFrame,
    .enhanced_render_frame = NULL,
    .save_name_prefix = "SuperMetroidDS",
    .state_save_extra = NULL,
    .state_load_extra = NULL,
    .on_state_loaded = NULL,
    .session_reset = NULL,
    .tier2_capture = 0,
    .minimum_state_version = 0,
    .exec_state_bound = NULL,
    .exec_state_save = NULL,
    .exec_state_load = NULL,
};