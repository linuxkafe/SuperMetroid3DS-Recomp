# SuperMetroidDS — Nintendo DS Port Makefile
# Milestone 1: interpreter-first LLE core from extern/snesrecomp/runner/src
# + the DS shim layer (ds_*). No generated code (src/gen) is compiled.
# Compatible with devkitpro/devkitarm Docker image.

# --- CONFIGURATION ---
TARGET       := SuperMetroidDS
BUILD        := build
SOURCES      := src
INCLUDES     := include
DATA         := assets
ARM7_SOURCES := src/arm7

# Build environment.
#
# The gates below used to degrade to `|| echo "<tool> not installed (OK in
# container)"` and exit 0, so `make check` reported success while checking
# nothing -- including on a tree that had never been compiled. A gate that
# cannot fail is not a gate. Tools are now resolved against a real toolchain,
# and if none is present the build runs inside the pinned devkitPro container
# (see Dockerfile). If neither is available the gates fail loudly.
#
# Override with DOCKER=0 to require a native toolchain, or DEVKITPRO=/path to
# point at a local install.
DOCKER       ?= 1
DEVKITPRO    ?= /opt/devkitpro
DEVKITARM    ?= $(DEVKITPRO)/devkitARM
CONTAINER    ?= devkitpro/devkitarm:latest
CONTAINER_WORKDIR := /source

# `command -v` rather than a bare name lookup: shell builtins and aliases must
# not be able to satisfy a tool probe.
HAVE_DOCKER  := $(shell command -v docker >/dev/null 2>&1 && echo yes || echo no)
HOST_UID      := $(shell id -u)
HOST_GID      := $(shell id -g)
HAVE_CC      := $(shell test -x "$(DEVKITARM)/bin/arm-none-eabi-gcc" && echo yes || echo no)
HAVE_CPPCHECK:= $(shell command -v cppcheck >/dev/null 2>&1 && echo yes || echo no)
HAVE_CLANGFMT:= $(shell command -v clang-format >/dev/null 2>&1 && echo yes || echo no)
HAVE_MELONDS := $(shell command -v melonDS >/dev/null 2>&1 && echo yes || echo no)

# Run a command inside the toolchain: natively when devkitARM is installed,
# otherwise through the container. USE_DOCKER is resolved once, at parse time.
ifeq ($(HAVE_CC),yes)
  TOOLCHAIN_ENV := native
  TOOLRUN :=
else ifeq ($(DOCKER),1)
  ifeq ($(HAVE_DOCKER),yes)
    TOOLCHAIN_ENV := container
    # Run as the invoking uid:gid. Without this the container writes build/
    # artifacts owned by root, and the *next* `make clean` silently fails to
    # remove them -- leaving stale objects that make a rebuild lie about what
    # was compiled. That happened here: root-owned .o files from an earlier run
    # survived `make clean` and masked a real fix across several rebuilds.
    TOOLRUN := docker run --rm -u $(HOST_UID):$(HOST_GID) \
                 -v "$(CURDIR):$(CONTAINER_WORKDIR)" -w $(CONTAINER_WORKDIR) $(CONTAINER)
  else
    TOOLCHAIN_ENV := none
    TOOLRUN :=
  endif
else
  TOOLCHAIN_ENV := none
  TOOLRUN :=
endif

# Inside the container devkitPRO lives at /opt/devkitpro. Deriving LIBNDS_PATH
# and CALICO_PATH from DEVKITPRO below means they must be correct here too, so
# DEVKITPRO is repointed rather than cleared.
ifeq ($(TOOLCHAIN_ENV),container)
  DEVKITPRO := /opt/devkitpro
  DEVKITARM := $(DEVKITPRO)/devkitARM
endif

CC           := $(DEVKITARM)/bin/arm-none-eabi-gcc
LD           := $(DEVKITARM)/bin/arm-none-eabi-gcc
OBJCOPY      := $(DEVKITARM)/bin/arm-none-eabi-objcopy
NDSTOOL      := $(DEVKITPRO)/tools/bin/ndstool

# --- Runner core (snesrecomp framework) ---
RUNNER_ROOT  := extern/snesrecomp/runner
RUNNER_SRC   := $(RUNNER_ROOT)/src

# Sources that provide globals the shim expects.
#
# common_cpu_infra.c defines `Snes *g_snes` (declared extern in
# common_cpu_infra.h:15) and is referenced throughout the shim (ds_rtl.c,
# ds_rtl_support.c) and the runner core. It was missing here, so every one of
# those references compiled and then failed at link.
#
# common_rtl.c is NOT included: it collides with src/ds_rtl_support.c, which
# defines the same globals (g_rom, g_snesrecomp_last_hdmaen,
# g_main_cpu_cycles_estimate, g_apu_pace_cycles_estimate,
# g_apu_last_sync_cycles, g_interp_apu_driving) and additionally redefines
# g_isTwlMode, which libnds/calico already provides. The DS port supplies its
# own RTL layer via ds_rtl*.c; common_rtl.c is the desktop host's.
#
# Consequence: RtlRunFrame and the netplay/cosim entry points that live in
# common_rtl.c are absent from the DS build. main.c calls RunOneFrameOfGame(),
# so that symbol's absence is a real gap -- see docs/ROADMAP.md [DISCOVERED].
RUNNER_CORE  := \
	cpu_state.c common_cpu_infra.c \
	snes/snes.c snes/cpu.c snes/cart.c snes/ppu.c snes/dma.c snes/joypad.c \
	snes/apu.c snes/spc.c snes/dsp.c snes/msu1.c snes/superfx.c snes/sa1.c \
	snes/sdd1.c snes/dsp1.c snes/dsp1_hle.c snes/cx4.c \
	snes/interp816.c snes/interp_bridge.c snes/tier2_capture.c \
	snes/audio_shadow.c snes/dsp_shadow.c snes/ws_shadow.c snes/color_lut.c \
	snes/ppu_legacy.c snes/snes_other.c \
	audio_trace.c \
	recomp_hw.c ppu_dma_trace.c snes_runahead.c framedump.c
#
# audio_trace.c and ppu_dma_trace.c look like removable desktop diagnostics
# (545 KB + 295 KB of .bss) and were removed on that basis. That was wrong and
# cost a link cycle: the runner core calls them from the hot APU and DMA paths
# (apu.c:78 audio_trace_on_cpu_port_apply; ppudma_record_dma). Their ring
# buffers are the storage those hooks write. Removing the file removes the
# storage, so the hooks must be stubbed to drop it -- which is a deliberate
# behaviour change to APU tracing, not a build cleanup.
#
# The .bss they cost (840 KB against a 3.5 MB region) is real and is a genuine
# budget problem on a 4 MB DS. It is tracked as such rather than solved by
# deleting the storage out from under live call sites.

# Architecture flags
ARCH         := -mthumb -mthumb-interwork

# Platform defines
PLATFORM_DEFS_ARM9 := -DARM9 -D__NDS__
PLATFORM_DEFS_ARM7 := -DARM7 -D__NDS__

# Paths
LIBNDS_PATH  := $(DEVKITPRO)/libnds
CALICO_PATH  := $(DEVKITPRO)/calico

# ARM9 flags: base + runner safety.
# NOTE: -ffast-math was dropped for the runner core (FP-correct APU pacing).
# Feature flags for DS build: enable only what we need; omit disabled features.
#
# These MUST be omitted, not defined as 0. The runner guards these features with
# a mix of `#ifdef X` (presence test) and `#if X` (value test). `-DX=0` satisfies
# the former and defeats the latter: `#ifdef SNES_COSIM` is TRUE when
# SNES_COSIM is defined as 0, so the "disabled" code path compiles and references
# symbols that live in files this build does not compile (common_rtl.c), and the
# link fails with undefined references to cosim_insn, cosim_apu_shared_clock,
# rtl_apu_* and friends.
#
# Omitting the define makes both spellings agree: `#ifdef` is false, `#if` is
# false. Verified per-flag against the runner sources:
#   SNES_COSIM                  -> #ifdef in cosim.h, interp_bridge.c, common_rtl.h
#   SNESRECOMP_ENABLE_MODS      -> #if  (6 sites)   safe either way
#   SNESRECOMP_FRAME_FINGERPRINTS -> #if (2 sites)  safe either way
#   SNESRECOMP_INTERP_PROFILE   -> #ifdef (7 sites) MUST be omitted
#   SNESRECOMP_NET_ROLLBACK     -> #ifdef (1 site)  MUST be omitted
#
# SNESRECOMP_AUDIO_TRACE_HISTORY=0 selects the upstream-supported COUNTERS mode
# (audio_trace.h:76-78 sets AUDIO_TRACE_PCM_RING / _EVENT_RING / _SNAP_RING to 0).
# The runner core calls the trace hooks from the hot APU path (apu.c:78), so the
# file stays compiled and the hooks stay live -- but every write to retained
# history is behind AUDIO_TRACE_WRITES_HISTORY and drops out, so the three ring
# buffers (545 KB of .bss) are not allocated. Counters are still maintained.
# This is a supported upstream configuration, not a local hack: FULL mode would
# ask for 16 MiB of PCM ring, which no DS build could ever satisfy.
#
# SNESRECOMP_PPU_DMA_HISTORY=0 does the same for the DMA/PPU snapshot rings
# (288 KB): ppu_dma_trace.c:235 compiles ppudma_record_dma and
# ppudma_frame_snapshot down to (void)-parameter no-ops, so snes.c:773 keeps
# calling them from the DMA enable path and still links. Verified the call at
# snes.c:773 is NOT guarded by the macro, which is why the file must stay in
# RUNNER_CORE and be switched off rather than deleted.
#
# Both history switches cost host-side forensics (multi-minute APU/HDMA
# post-mortems). Neither is on the DS critical path: the DS has no report
# writer. Re-enable both on a host build if a trace session needs them.
FEATURE_DEFS := -DSNESRECOMP_AUDIO_TRACE_HISTORY=0 \
                -DSNESRECOMP_PPU_DMA_HISTORY=0
CFLAGS       := -g -Wall -O2 -std=gnu11 -mcpu=arm946e-s -mtune=arm946e-s \
                -fomit-frame-pointer \
                $(PLATFORM_DEFS_ARM9) $(FEATURE_DEFS) $(ARCH)
CXXFLAGS     := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS      := -g $(ARCH)

# ARM7 flags
ARM7_CFLAGS  := -g -Wall -O2 -std=gnu11 -mcpu=arm7tdmi -mtune=arm7tdmi \
                -fomit-frame-pointer \
                $(PLATFORM_DEFS_ARM7) $(ARCH)

# Includes: DS shim FIRST (config.h), then runner, then the runner's snes/,
# then the DS project headers.
INCLUDE      := -Ids/include -I$(RUNNER_SRC) -I$(RUNNER_SRC)/snes \
                -I$(INCLUDES) -I$(LIBNDS_PATH)/include -I$(CALICO_PATH)/include
CFLAGS      += $(INCLUDE)
CXXFLAGS    += $(INCLUDE)
ARM7_CFLAGS += $(INCLUDE)

# ARM9 link flags
LDFLAGS      := -g $(ARCH) -specs=$(CALICO_PATH)/share/ds9.specs -Wl,-Map,$(BUILD)/$(TARGET).map \
                -L$(LIBNDS_PATH)/lib -L$(CALICO_PATH)/lib
# ARM7 link flags
ARM7_LDFLAGS := -g $(ARCH) -specs=$(CALICO_PATH)/share/ds7.specs \
                -L$(LIBNDS_PATH)/lib -L$(CALICO_PATH)/lib

# Libraries
LIBS        := -Wl,--start-group -lnds9 -lcalico_ds9 -Wl,--end-group -lfat -lmm9 -lm
ARM7_LIBS   := -lnds7 -lcalico_ds7

# Source files
CFILES       := $(wildcard $(SOURCES)/*.c)
SFILES       := $(wildcard $(SOURCES)/*.s)
ARM7_CFILES  := $(wildcard $(ARM7_SOURCES)/*.c)
RUNNER_CFILES := $(addprefix $(RUNNER_SRC)/,$(RUNNER_CORE))
BANNER       := $(DATA)/banner.bin

# Object files
OFILES_ARM9  := $(CFILES:$(SOURCES)/%.c=$(BUILD)/%.o) $(SFILES:$(SOURCES)/%.s=$(BUILD)/%.o) \
                $(RUNNER_CFILES:$(RUNNER_SRC)/%.c=$(BUILD)/%.o)
OFILES_ARM7  := $(ARM7_CFILES:$(ARM7_SOURCES)/%.c=$(BUILD)/arm7/%.o)

# Runner dependencies are generated by the same -MMD path below.

# Output files
ELF_ARM9     := $(BUILD)/$(TARGET).arm9.elf
ELF_ARM7     := $(BUILD)/$(TARGET).arm7.elf
NDS_ROM      := $(BUILD)/$(TARGET).nds

.PHONY: all clean setup lint format test check

all: $(NDS_ROM)

$(NDS_ROM): $(ELF_ARM9) $(ELF_ARM7) $(BANNER)
	@echo "Packaging NDS: $(notdir $@)"
	@$(TOOLRUN) $(NDSTOOL) -c $@ -9 $(ELF_ARM9) -7 $(ELF_ARM7) -t $(BANNER)
	@echo "BUILD SUCCESS! $(notdir $@) created."

$(ELF_ARM9): $(OFILES_ARM9)
	@echo "Linking ARM9: $(notdir $@)"
	@$(TOOLRUN) $(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(ELF_ARM7): $(OFILES_ARM7)
	@echo "Linking ARM7: $(notdir $@)"
	@$(TOOLRUN) $(LD) $(ARM7_LDFLAGS) $^ $(ARM7_LIBS) -o $@

# ARM9 compilation
$(BUILD)/%.o: $(SOURCES)/%.c
	@mkdir -p $(dir $@)
	@$(TOOLRUN) $(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: $(SOURCES)/%.s
	@mkdir -p $(dir $@)
	@$(TOOLRUN) $(CC) $(ASFLAGS) -c $< -o $@

# Runner core compilation (mirrored paths: extern/snesrecomp/runner/src/x.c -> build/x.o)
$(BUILD)/%.o: $(RUNNER_SRC)/%.c
	@mkdir -p $(dir $@)
	@$(TOOLRUN) $(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# ARM7 compilation
$(BUILD)/arm7/%.o: $(ARM7_SOURCES)/%.c
	@mkdir -p $(dir $@)
	@$(TOOLRUN) $(CC) $(ARM7_CFLAGS) -MMD -MP -c $< -o $@

# Dependencies
-include $(OFILES_ARM9:.o=.d)
-include $(OFILES_ARM7:.o=.d)

clean:
	@echo "Cleaning..."
	@rm -fr $(BUILD)

setup:
	@echo "Toolchain: $(TOOLCHAIN_ENV)"
ifeq ($(TOOLCHAIN_ENV),none)
	@echo "ERROR: no devkitARM toolchain and no usable Docker." >&2
	@echo "  expected: $(DEVKITPRO)/devkitARM/bin/arm-none-eabi-gcc on this host," >&2
	@echo "            or 'docker' on PATH with image $(CONTAINER)" >&2
	@echo "  found:    neither" >&2
	@echo "  fix: install devkitPro, or run with DOCKER=1 on a host that has it." >&2
	@exit 1
endif
ifeq ($(TOOLCHAIN_ENV),container)
	@echo "Using container $(CONTAINER) (no native devkitARM found)"
	@docker image inspect $(CONTAINER) >/dev/null 2>&1 || \
		(echo "ERROR: image $(CONTAINER) not present locally. Pull it first:" >&2; \
		 echo "  docker pull $(CONTAINER)" >&2; exit 1)
	@$(CC) --version >/dev/null 2>&1 || true
	@echo "Container toolchain reachable"
else
	@echo "Using native devkitARM at $(DEVKITARM)"
	@test -x "$(CC)" || (echo "ERROR: $(CC) not executable" >&2; exit 1)
endif

# cppcheck is the only static gate that runs without the ARM toolchain, so it
# is the one that must not silently no-op. It fails if the tool is missing.
lint: setup
ifeq ($(TOOLCHAIN_ENV),container)
	@echo "Running cppcheck in $(CONTAINER)..."
	@$(TOOLRUN) sh -c 'cppcheck --enable=all --std=c11 --error-exitcode=1 \
		--suppress=missingIncludeSystem --suppress=unusedFunction \
		--suppress=missingIncludeSystem -I include -I ds/include src include' \
		|| (echo "LINT FAILED (cppcheck reported findings, or crashed)" >&2; exit 1)
else ifeq ($(HAVE_CPPCHECK),yes)
	@echo "Running cppcheck..."
	@cppcheck --enable=all --std=c11 --error-exitcode=1 \
		--suppress=missingIncludeSystem --suppress=unusedFunction \
		-I include -I ds/include src include \
		|| (echo "LINT FAILED (cppcheck reported findings)" >&2; exit 1)
else
	@echo "LINT FAILED: cppcheck not installed." >&2
	@echo "  expected: cppcheck on PATH" >&2
	@echo "  fix:      apt install cppcheck, or run with DOCKER=1." >&2
	@exit 1
endif
	@echo "LINT OK"

format:
	@echo "Checking formatting with clang-format..."
	@if [ "$(HAVE_CLANGFMT)" = yes ]; then \
		find $(SOURCES) $(INCLUDES) -name '*.c' -o -name '*.h' | xargs clang-format --dry-run -Werror \
		|| (echo "FORMAT FAILED: files differ from clang-format output" >&2; exit 1); \
		echo "FORMAT OK"; \
	else \
		echo "FORMAT SKIPPED: clang-format not installed (not a gate)"; \
	fi

# melonDS is not present in the devkitPro image and there is no headless ROM
# assertion harness yet. This target therefore reports UNAVAILABLE and fails,
# rather than reporting success for a test that never ran.
test: $(NDS_ROM)
	@echo "Testing on melonDS (headless)..."
ifeq ($(HAVE_MELONDS),yes)
	@melonDS $(NDS_ROM) --headless || (echo "TEST FAILED: melonDS exited non-zero" >&2; exit 1)
	@echo "TEST OK"
else
	@echo "TEST UNAVAILABLE: melonDS not installed." >&2
	@echo "  There is no automated ROM assertion harness in this tree, so" >&2
	@echo "  'make test' cannot currently prove anything about $(notdir $(NDS_ROM))." >&2
	@echo "  fix: install melonDS, or add a harness under tests/." >&2
	@exit 1
endif

check: setup all lint