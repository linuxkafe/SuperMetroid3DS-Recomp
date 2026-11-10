# SuperMetroid3DS — Nintendo 3DS build.
#
# WHAT THIS BUILDS
#   smrecomp_core  STATIC  the Super Metroid static recompilation (upstream)
#   supermetroid3ds 3DSX   this port's frontend: video, input, audio, loop
#
# THE UPSTREAM RULE
#   SuperMetroidRecomp is consumed READ-ONLY from outside this repository. It
#   is never edited in place and never vendored as a submodule. Two reasons,
#   both learned the hard way:
#
#     1. The DS attempt edited extern/snesrecomp/runner/src/snes/ppu.c directly,
#        injecting a libnds macro into the framework. That edit cannot compile
#        on any other platform, and it is preserved at
#        legacy/patches/0001-nds-ppu-t0-decl.patch on the ds-legacy branch as
#        evidence of the failure mode rather than as a patch to apply.
#
#     2. An in-place edit is invisible in a diff of THIS repository. Reviewing
#        a port means reading its own changes; changes that live in someone
#        else's tree do not appear.
#
#   Where a change genuinely cannot be expressed in frontend/3ds, it goes in
#   patches/NNNN-*.patch and is applied by `make prepare`. The chain is applied
#   in filename order to a writable mirror, so a clean checkout reproduces the
#   same tree byte for byte.
#
# GENERATED CODE
#   src/gen/*.c (~88 MB of C, 116 files) and recomp/funcs.h are GENERATED from
#   the ROM by upstream's v2_regen.py. They are gitignored by upstream too,
#   because they are derived from copyrighted ROM data. They are built from
#   -DGEN_DIR, never committed. See docs/3DS-PORT-STUDY.md.

cmake_minimum_required(VERSION 3.22)
project(SuperMetroid3DS C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Upstream and generated locations ────────────────────────────────────────
# Both are absolute paths outside this tree. UPSTREAM_ROOT is the read-only
# v0.3.11 checkout; GEN_DIR holds the regenerated core.
set(UPSTREAM_ROOT "" CACHE PATH "Read-only SuperMetroidRecomp checkout (v0.3.11)")
set(GEN_DIR "" CACHE PATH "Directory holding regenerated src/gen/*.c and funcs.h")

# The worktree is where patches/ is applied and where regen writes src/gen and
# recomp/funcs.h. The build compiles from it rather than from the read-only
# upstream, because the upstream must stay pristine — the whole point of the
# patch chain is that it is applied to a writable mirror, not in place.
set(WORKTREE_ROOT "" CACHE PATH
    "Writable mirror of the upstream: patches/ applied here, regen output here")

if(NOT WORKTREE_ROOT)
    message(FATAL_ERROR
        "WORKTREE_ROOT is not set. Run this through the Makefile, or pass "
        "-DWORKTREE_ROOT=/path/to/SuperMetroidRecomp-work. The upstream "
        "checkout is deliberately external and read-only.")
endif()

set(SR "${WORKTREE_ROOT}/snesrecomp/runner/src")
set(GAME_SRC "${WORKTREE_ROOT}/src")

foreach(_req "${SR}" "${GAME_SRC}")
    if(NOT EXISTS "${_req}")
        message(FATAL_ERROR "UPSTREAM_ROOT='${UPSTREAM_ROOT}' is missing ${_req}")
    endif()
endforeach()

# ── ROM identity header (T033) ─────────────────────────────────────────────
# Upstream keeps the ROM's expected digests in ONE text file, rom_identity.txt,
# read by its build, its regen tool and its release workflow. This port needs the
# same values at compile time so it can refuse a ROM that is not this ROM.
#
# It is GENERATED from that file, not written out by hand here. A literal sha256
# pasted into CMake would be a second source of truth: a ROM revision upstream
# would update one and silently miss the other, and the port would then refuse a
# correct ROM or accept a wrong one with no compile-time signal.
#
# The generator is upstream's own snesrecomp/tools/rom_identity.py, so the
# emitted header is byte-identical to the one upstream's CMake emits — which is
# what keeps the two from drifting.
set(ROM_IDENTITY "${UPSTREAM_ROOT}/rom_identity.txt")
set(ROMID_TOOL "${UPSTREAM_ROOT}/snesrecomp/tools/rom_identity.py")
set(ROM_IDENTITY_DIR "${CMAKE_BINARY_DIR}/romid")

if(NOT EXISTS "${ROM_IDENTITY}" OR NOT EXISTS "${ROMID_TOOL}")
    message(FATAL_ERROR
        "ROM identity inputs missing from UPSTREAM_ROOT='${UPSTREAM_ROOT}'. "
        "Expected rom_identity.txt and snesrecomp/tools/rom_identity.py.")
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    message(FATAL_ERROR
        "python3 is required to generate snesrecomp_rom_identity.h from "
        "rom_identity.txt. The devkitPro image ships one; a native build needs "
        "it on PATH.")
endif()

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${ROMID_TOOL}" "${ROM_IDENTITY}"
            --header "${ROM_IDENTITY_DIR}"
    RESULT_VARIABLE _romid_rc
    OUTPUT_VARIABLE _romid_out
    ERROR_VARIABLE  _romid_err)
if(NOT _romid_rc EQUAL 0)
    message(FATAL_ERROR
        "rom_identity.py failed (${_romid_rc}):\n${_romid_err}")
endif()
message(STATUS "ROM identity: ${ROM_IDENTITY_DIR}/snesrecomp_rom_identity.h")

# ── Platform: Nintendo 3DS ─────────────────────────────────────────────────
if(NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "DEVKITPRO is not set. Build through the Makefile (docker-build).")
endif()
find_path(CTR_INCLUDE_DIR 3ds.h HINTS "$ENV{DEVKITPRO}/libctru/include" NO_CMAKE_FIND_ROOT_PATH)
find_library(CTRU_LIB NAMES ctru HINTS "$ENV{DEVKITPRO}/libctru/lib" NO_CMAKE_FIND_ROOT_PATH)
if(NOT CTRU_LIB OR NOT CTR_INCLUDE_DIR)
    message(FATAL_ERROR "libctru not found under $ENV{DEVKITPRO}. Install 3ds-dev.")
endif()
message(STATUS "libctru: ${CTRU_LIB}")

# ── Core option (before add_executable so frontend gets correct defs) ────────
option(SM_WITH_CORE "Link the recompiled core (requires T027: ARM11 fiber backend)" OFF)

if(SM_WITH_CORE AND NOT GEN_DIR)
    message(FATAL_ERROR "SM_WITH_CORE=ON requires -DGEN_DIR pointing at regenerated src/gen.")
endif()

# ── Frontend (always built; test pattern when core is off) ──────────────────
add_executable(supermetroid3ds
    frontend/3ds/3ds_main.c
    frontend/3ds/3ds_render.c
    frontend/3ds/3ds_input.c
    frontend/3ds/3ds_audio.c
)

if(SM_WITH_CORE)
    target_sources(supermetroid3ds PRIVATE platform/3ds/host.c)
endif()

target_include_directories(supermetroid3ds PRIVATE
    frontend/3ds
    ${ROM_IDENTITY_DIR}
)
target_compile_options(supermetroid3ds PRIVATE -Wall -Wextra -Werror)

# The compile definition must be set immediately after target creation so
# CMake tracks it for all sources in the target. Doing it later in a separate
# if() block does not invalidate already-built object files.
if(SM_WITH_CORE)
    target_compile_definitions(supermetroid3ds PRIVATE SM_WITH_CORE=1)
endif()

# ── Recompiled core (optional) ──────────────────────────────────────────────
if(SM_WITH_CORE)
    message(STATUS "SM core: building from GEN_DIR=${GEN_DIR}")

    file(GLOB SM_GEN_SOURCES "${GEN_DIR}/*.c")
    list(LENGTH SM_GEN_SOURCES SM_GEN_COUNT)
    if(SM_GEN_COUNT EQUAL 0)
        message(FATAL_ERROR
            "No .c files in GEN_DIR='${GEN_DIR}'. Run `make regen` — the core is "
            "generated from the ROM and is never committed.")
    endif()
    message(STATUS "SM core: ${SM_GEN_COUNT} generated translation units")

    # Upstream sources that carry the game. src/main.c is EXCLUDED: it is the
    # SDL host shim, and this frontend replaces it. src/sm_mods.c is EXCLUDED:
    # it needs recomp-ui's launcher headers, which do not exist here.
    set(SM_GAME_SOURCES
        ${GAME_SRC}/sm_rtl.c          # THE PORT: single-fiber frame model, NMI/IRQ, HDMA
        ${GAME_SRC}/sm_cpu_infra.c    # RtlGameInfo registration
        ${GAME_SRC}/sm_post_mortem.c  # framework crash-report hook
        ${GAME_SRC}/sm_spc_player.c   # SPC upload interception
        ${GAME_SRC}/sm_renderer.c     # custom renderer
        ${GAME_SRC}/sm_mode7.c        # SM's mode-7 object projection
        ${GAME_SRC}/sm_display.c      # presentation geometry
        ${GAME_SRC}/sm_video.c        # aspect / HUD policy
        # sm_replay.c excluded: depends on desktop-only replay writer and config
        # sm_startup.c excluded: depends on host_paths/host_main (desktop-only)
        # Stubs for SmReplay* are provided in platform/3ds/host.c
        ${GAME_SRC}/gen_stubs.c       # hand-written bodies for hle_func targets
    )

    # Framework sources. Every entry was syntax-checked for ARM11; see
    # docs/3DS-PORT-STUDY.md for the measured 71/80 result and the 9 exclusions.
    # The exclusions are recorded in CMAKELists.txt of the port study so that
    # "why is this file missing" has an answer that is not a guess.
    set(SM_FRAMEWORK_SOURCES
        ${SR}/common_cpu_infra.c
        ${SR}/common_rtl.c            # needs the 3DS fiber shim (T027)
        ${SR}/widescreen.c
        ${SR}/recomp_hw.c
        ${SR}/framedump.c
        ${SR}/state_dump.c
        ${SR}/host_paths.c
        ${SR}/host_args.c
        ${SR}/launcher.c
        ${SR}/launcher_cache.c
        ${SR}/launcher_picker.c
        ${SR}/rom_image_verify.c
        ${SR}/crc32.c
        ${SR}/snapshot_guard.c
        ${SR}/sha256.c
        # keybinds.c is deliberately NOT built: it is SDL keyboard only, and
        # the 3DS frontend maps input directly through hidScanInput.
        ${SR}/snes_overlay_draw.c
        ${SR}/snes_runahead.c
        # snes_savestate_menu.c, snes_osd.c, keybinds.c and host_report.c are
        # deliberately NOT built. The comments above kept claiming they were
        # excluded while the files stayed in the list, and each one is SDL-only
        # or needs a POSIX header the 3DS lacks — they fail to compile.
        ${SR}/netplay/snes_netplay.c
        ${SR}/snes_rewind.c
        ${SR}/cpu_state.c
        ${SR}/program_module.c
        ${SR}/rom_patch.c
        ${SR}/generic_frame_driver.c
        ${SR}/beam_frame_driver.c
        ${SR}/content_variant.c
        ${SR}/variant_selector.c
        ${SR}/cpu_trace.c
        ${SR}/audio_trace.c
        ${SR}/ppu_dma_trace.c
        # host_report.c is deliberately NOT built: it needs <sys/utsname.h>,
        # which 3DS newlib lacks.
        ${SR}/execution_mode.c
        ${SR}/mod_audio.c
        ${SR}/host_mesh.c
        ${SR}/host_mesh_builder.c
        ${SR}/guarded_patch.c
        ${SR}/util.c
        # The cooperative fiber API. This is T027: implementing fiber switching for
        # 3DS ARM11. The upstream fiber_compat.c supports Win32, ucontext (macOS),
        # and pthread (Android) — not 3DS. We provide our own ARM11 implementation.
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/3ds/fiber_compat.c
        # --- SNES hardware model (the LLE floor) ---
        ${SR}/snes/apu.c
        ${SR}/snes/cart.c
        ${SR}/snes/cpu.c
        ${SR}/snes/dma.c
        ${SR}/snes/dsp.c
        ${SR}/snes/dsp1.c
        ${SR}/snes/dsp1_hle.c
        ${SR}/snes/joypad.c
        ${SR}/snes/audio_shadow.c
        ${SR}/snes/dsp_shadow.c
        ${SR}/snes/msu1.c
        ${SR}/snes/color_lut.c
        ${SR}/snes/ppu.c
        ${SR}/snes/ppu_legacy.c
        ${SR}/snes/sa1.c
        ${SR}/snes/sdd1.c
        ${SR}/snes/ws_shadow.c
        ${SR}/snes/snes.c
        ${SR}/snes/snes_other.c
        ${SR}/snes/spc.c
        ${SR}/snes/superfx.c
        ${SR}/snes/interp816.c
        ${SR}/snes/tier2_capture.c
        ${SR}/snes/interp_bridge.c
        ${SR}/snes/cx4.c
    )

    # Files deliberately NOT built, and why. Kept here so the exclusion list is
    # a decision on the record rather than an absence.
    #
    #   desktop/sdl_compat.h  -> platform/3ds/sdl_compat.h   (T027)
    #   keybinds.c             -> SDL keyboard
    #   snes_osd.c             -> SDL text
    #   snes_savestate_menu.c  -> SDL menu
    #   host_report.c          -> needs <sys/utsname.h>, which 3DS newlib lacks
    #   lua_bridge.c           -> Lua modding, not part of a 3DS homebrew
    #   setup_host_dispatch.c  -> setup-host builds only
    #   content_patch.c        -> host-side mod patcher
    #   benchmark.c, cosim*.c  -> host profiling / co-simulation harnesses
    #   src/main.c             -> SDL host shim, replaced by frontend/3ds
    #   src/sm_mods.c          -> needs recomp-ui launcher headers
    #
    # NOT excluded for lack of sockets: 3DS DOES have BSD sockets, via libctru's
    # soc service (<sys/socket.h> + socInit). An earlier probe of this port
    # wrongly concluded otherwise because it omitted -I$DEVKITPRO/libctru/include.
    # The consequence matters — the TCP debug bridge is viable, and it is what
    # makes ports 5003/5004 reachable (T028). debug_server.c is held back for a
    # different and weaker reason: it is a host debugging surface, not part of a
    # shipping port, and socInit ownership belongs to the frontend, not the core.

    add_library(smrecomp_core STATIC ${SM_GAME_SOURCES} ${SM_FRAMEWORK_SOURCES} ${SM_GEN_SOURCES})

    # Generated files include "funcs.h" flat, so GEN_DIR goes on the path for
    # both the generated sources and funcs.h itself.
    target_include_directories(smrecomp_core PRIVATE
        platform/3ds
        ${SR}/desktop
        ${SR}
        ${GAME_SRC}
        ${WORKTREE_ROOT}/recomp
        ${GEN_DIR}
    )
    target_compile_definitions(smrecomp_core PRIVATE
        SNESRECOMP_ROM_IDENTITY_HEADER="snesrecomp_rom_identity.h"
        __SM3DS__
    )

    target_link_libraries(supermetroid3ds PRIVATE smrecomp_core)
endif()

# ── Link ───────────────────────────────────────────────────────────────────
target_include_directories(supermetroid3ds PRIVATE
    ${CTR_INCLUDE_DIR}
    platform/3ds
    ${WORKTREE_ROOT}/snesrecomp/runner/src
    ${WORKTREE_ROOT}/snesrecomp/runner/src/desktop
    ${GAME_SRC}
)
target_link_libraries(supermetroid3ds PRIVATE ${CTRU_LIB} m)

# 3DSX (homebrew) output. No ROM is embedded: the port loads the .sfc from the
# SD card at runtime, so the build artifact contains no copyrighted bytes.
set_target_properties(supermetroid3ds PROPERTIES
    OUTPUT_NAME "supermetroid3ds"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    BUILD_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# NOTE: -specs=3dsx.specs is deliberately NOT set here. devkitARM.cmake (pulled
# in by 3DS.cmake) already passes it, and passing it twice makes gcc process
# 3dsx.specs -- which %includes sync-dmb.specs -- a second time, failing with
# "attempt to rename spec 'link' to already defined spec 'sync_sync_link'".
target_link_options(supermetroid3ds PRIVATE
    -Wl,--gc-sections
    -Wl,-Map,${CMAKE_BINARY_DIR}/supermetroid3ds.map
)

# ── .3dsx packaging ───────────────────────────────────────────────────────
# 3DS.cmake locates 3dsxtool but does not invoke it; the .3dsx is what a
# homebrew loader and an emulator actually take, so producing only the .elf
# would leave `make test` with nothing to run.
find_program(CTR_3DSXTOOL NAMES 3dsxtool HINTS "$ENV{DEVKITPRO}/tools/bin" NO_CMAKE_FIND_ROOT_PATH)
if(CTR_3DSXTOOL)
    add_custom_command(TARGET supermetroid3ds POST_BUILD
        COMMAND ${CTR_3DSXTOOL}
                $<TARGET_FILE:supermetroid3ds>
                ${CMAKE_BINARY_DIR}/supermetroid3ds.3dsx
        COMMENT "Packaging supermetroid3ds.3dsx"
        VERBATIM)
else()
    message(WARNING
        "3dsxtool not found — only the .elf will be produced, and the emulator "
        "gate cannot run. Install 3dstools.")
endif()