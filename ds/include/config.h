#ifndef SNESRECOMP_DS_CONFIG_H
#define SNESRECOMP_DS_CONFIG_H
/*
 * DS replacement for the desktop runner's config.h.
 *
 * The desktop config.h pulls in sdl_compat.h and a runtime `Config` struct
 * that only the desktop host (host_main.c, host_report.c) touches. The DS
 * port compiles the framework core (snes/ + common_cpu_infra.c) which only
 * needs the include site to exist — common_rtl.c, the only other consumer,
 * is intentionally NOT compiled here (see ds_rtl_support.c). If a future
 * port file needs a real config structure it is added here, explicitly for
 * DS.
 */
#endif /* SNESRECOMP_DS_CONFIG_H */