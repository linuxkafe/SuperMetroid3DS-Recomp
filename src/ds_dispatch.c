/* Dispatch tables for the DS port: an interpreter-first build with no
 * recompiled code.
 *
 * This mirrors the framework's setup_host_dispatch.c, but deliberately does
 * NOT define SNESRECOMP_SETUP_HOST: a setup host is a machine that refuses
 * to boot a guest, and this port is anything but — it boots the real ROM
 * through the LLE interpreter (interp_bridge_run_loop) for everything.
 *
 * The recompiler normally emits these tables into src/gen/dispatch_v2.c;
 * the DS build has no src/gen (it cannot — 91 MB of generated code cannot
 * fit a 4 MB DS), so the tables are empty. An empty table is exactly the
 * correct answer for an interpreter-first core: cpu_dispatch_pc() reports a
 * miss for every PC and no compiled body can run.
 */
#include "cpu_state.h"

const DispatchEntry g_dispatch_table[] = {
    { 0xFFFFFFFFu, { NULL, NULL, NULL, NULL }, 0 },
};
const unsigned g_dispatch_table_count = 0u;

const RamRoutineGuard g_ram_routine_guards[] = {
    { 0xFFFFFFFFu, 0u, 0u },
};
const unsigned g_ram_routine_guard_count =
    (unsigned)(sizeof(g_ram_routine_guards) / sizeof(g_ram_routine_guards[0]));