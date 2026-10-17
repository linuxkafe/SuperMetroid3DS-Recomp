# SuperMetroid3DS — build, test and environment helpers.
#
# The upstream SuperMetroidRecomp checkout is READ-ONLY and lives OUTSIDE this
# repository. See docs/3DS-PORT-STUDY.md for why, and CMakeLists.txt for the
# consequences. Nothing here ever writes to $(UPSTREAM).

DOCKER_IMAGE ?= devkitpro/devkitarm
WORK_DIR     ?= $(CURDIR)

# Read-only upstream, pinned to v0.3.11. Verify with `make upstream-check`.
UPSTREAM     ?= /home/seyon/dev/Games/PC/SuperMetroidRecomp
UPSTREAM_REF ?= v0.3.11

# Generated core output: ~88 MB of C derived from the ROM. Never committed.
GEN_DIR      ?= /home/seyon/dev/Games/PC/SuperMetroid3DS-build/gen

# The user's own legally obtained ROM. Not committed, not redistributed.
ROM_PATH     ?= $(CURDIR)/SuperMetroid-usa.sfc

# SimCity-3DS-Port owns TCP 5002. This port must not collide with it: two
# builds on the Deck running their debug bridges at once would cross-connect,
# and the resulting measurements would be garbage that looks real. Hence 5003
# (command protocol) and 5004 (GDB RSP). See CLAUDE.md.
SM_DBG_PORT  ?= 5003
SM_GDB_PORT  ?= 5004

# devkitPro ships toolchain files under /opt/devkitpro/cmake, NOT under
# $(DEVKITARM)/cmake/Platform. The old path does not exist in the image.
TOOLCHAIN    ?= /opt/devkitpro/cmake/3DS.cmake

# Headless emulator for behavioural tests. Azahar needs a display server even
# when it renders nothing, hence xvfb-run.
AZAHAR       ?= /home/seyon/dev/tools/azahar/usr/bin/azahar

.PHONY: all docker-build build clean doctor setup upstream-check regen \
        prepare size lint test check emulator-headless

all: docker-build

# ── Build ─────────────────────────────────────────────────────────────────

docker-build:
	@echo "==> Building SuperMetroid3DS (3DSX) in $(DOCKER_IMAGE)"
	docker run --rm \
		-v "$(UPSTREAM):/upstream:ro" \
		-v "$(WORK_DIR):/work" \
		-v "$(GEN_DIR):/gen" \
		-w /work \
		$(DOCKER_IMAGE) \
		bash -c '\
			set -e -o pipefail; \
			export DEVKITPRO=/opt/devkitpro DEVKITARM=$$DEVKITPRO/devkitARM; \
			export PATH=$$DEVKITARM/bin:$$PATH; \
			mkdir -p build3ds && cd build3ds && \
			cmake .. \
				-DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN) \
				-DCMAKE_BUILD_TYPE=Release \
				-DUPSTREAM_ROOT=/upstream \
				-DGEN_DIR=/gen && \
			make -j$$(nproc) 2>&1 | tail -60'

# Native build, if devkitARM is installed on the host.
build:
	@command -v arm-none-eabi-gcc >/dev/null 2>&1 || \
		{ echo "arm-none-eabi-gcc not found. Use 'make docker-build'."; exit 1; }
	@mkdir -p build3ds && cd build3ds && \
		cmake .. -DCMAKE_BUILD_TYPE=Release \
			-DUPSTREAM_ROOT=$(UPSTREAM) -DGEN_DIR=$(GEN_DIR) && \
		make -j$(shell nproc)

# ── Generated core ────────────────────────────────────────────────────────
# The recompiled C is generated from the ROM by upstream's own tooling. It is
# ~88 MB and takes a long time; that cost is why it is a separate target and not
# part of `make build`.
regen:
	@if [ ! -f "$(ROM_PATH)" ]; then \
		echo "ROM not found at $(ROM_PATH)."; \
		echo "Place a legally obtained Super Metroid (Japan, USA) .sfc there."; \
		echo "The expected sha256 is in \$$UPSTREAM/rom_identity.txt."; \
		exit 1; \
	fi
	@mkdir -p "$(GEN_DIR)"
	cd $(UPSTREAM) && python3 snesrecomp/tools/v2_regen.py \
		--rom "$(ROM_PATH)" \
		--cfg-dir recomp \
		--out-dir "$(GEN_DIR)" \
		--jobs $(shell nproc)
	@echo "==> Generated: $$(ls -1 $(GEN_DIR)/*.c 2>/dev/null | wc -l) translation units"

# `prepare` is the reproducibility contract: upstream is read-only, so nothing
# is patched in place. Today the patch chain is EMPTY because every change so
# far lives in frontend/3ds and platform/3ds. When it stops being empty, the
# patches apply to a writable mirror at $(GEN_DIR)/../upstream-work.
prepare: upstream-check
	@echo "==> Patch chain: $$(ls -1 patches/*.patch 2>/dev/null | wc -l) patches"
	@if [ -z "$$(ls patches/*.patch 2>/dev/null)" ]; then \
		echo "    (empty — upstream is used unmodified, which is the intended state)"; \
	fi

upstream-check:
	@if [ ! -d "$(UPSTREAM)" ]; then \
		echo "UPSTREAM not found at $(UPSTREAM)"; \
		echo "See 'make setup' for the clone command."; exit 1; \
	fi
	@v=$$(cat $(UPSTREAM)/VERSION 2>/dev/null); \
	 echo "==> upstream VERSION: $$v (expected $(UPSTREAM_REF:0.3.}=$(UPSTREAM_REF)))"; \
	 case "$$v" in \
	   0.3.11) ;; \
	   *) echo "WARNING: upstream is v$$v, not the pinned $(UPSTREAM_REF)."; \
	      echo "         Read docs/3DS-PORT-STUDY.md before changing the pin." ;; \
	 esac

# ── Size ──────────────────────────────────────────────────────────────────

size: build
	@ls -la build3ds/*.3dsx build3ds/*.elf 2>/dev/null || echo "No artifact. Run 'make build'."
	@echo "NOTE: cartridge headroom for 3DSX homebrew is ~128 MB, so a size"
	@echo "      problem here is a compile-time problem, not a fit problem."

# ── Quality gates ─────────────────────────────────────────────────────────
# `lint` runs cppcheck over THIS repository only. It deliberately does not walk
# $(UPSTREAM): those sources are read-only, vendored verbatim, and are upstream's
# to lint. The DS-era `make lint` reported 8 findings on a file it could not
# model, which is noise that trains people to ignore the gate.
lint:
	@docker run --rm -v "$(WORK_DIR):/work" -w /work $(DOCKER_IMAGE) \
		bash -c 'cppcheck --enable=warning,style --std=c11 --error-exitcode=1 \
			--suppress=missingIncludeSystem --quiet \
			-I frontend/3ds -I platform/3ds \
			frontend platform 2>&1' && echo "==> lint: clean"

# `test` builds and then runs the emulator gate. A build alone is not a test:
# the DS era's `test` target depended on the .nds and called that coverage.
test: docker-build
	@$(MAKE) --no-print-directory emulator-headless

# Defaults to the Deck, which is where this port is actually run. The local
# Azahar copy is a fallback for quick iteration.
emulator-headless:
	@HOST=deck bash scripts/gate-headless.sh build3ds/supermetroid3ds.3dsx 120

emulator-headless-local:
	@AZAHAR="$(AZAHAR)" bash scripts/gate-headless.sh build3ds/supermetroid3ds.3dsx 120

check: upstream-check lint build

# ── Environment ───────────────────────────────────────────────────────────

doctor:
	@echo "=== Docker ==="; docker --version 2>&1 || echo "  NOT INSTALLED"
	@echo "=== devkitPro image ==="; \
	  docker images -q $(DOCKER_IMAGE) >/dev/null 2>&1 && echo "  OK" || echo "  MISSING (make setup)"
	@echo "=== Upstream (read-only) ==="
	@if [ -d "$(UPSTREAM)" ]; then \
		echo "  OK: $(UPSTREAM)"; \
		echo "  VERSION: $$(cat $(UPSTREAM)/VERSION 2>/dev/null)"; \
		echo "  dirty files: $$(cd $(UPSTREAM) && git status --porcelain | wc -l) (must be 0)"; \
	else echo "  MISSING: $(UPSTREAM)"; fi
	@echo "=== ROM ==="
	@if [ -f "$(ROM_PATH)" ]; then \
		echo "  Found: $(ROM_PATH)"; \
		echo "  sha256: $$(sha256sum $(ROM_PATH) | cut -d' ' -f1)"; \
		echo "  expected: $$(grep expected_sha256 $(UPSTREAM)/rom_identity.txt 2>/dev/null | awk '{print $$3}')"; \
	else echo "  MISSING: $(ROM_PATH)"; fi
	@echo "=== Generated core ==="
	@echo "  GEN_DIR: $(GEN_DIR)"; \
	 echo "  units: $$(ls -1 $(GEN_DIR)/*.c 2>/dev/null | wc -l) (expect 0 until 'make regen')"
	@echo "=== Emulator ==="
	@if [ -x "$(AZAHAR)" ]; then echo "  OK: $(AZAHAR)"; else echo "  MISSING: $(AZAHAR)"; fi
	@command -v xvfb-run >/dev/null 2>&1 && echo "  OK: xvfb-run" || echo "  MISSING: xvfb-run"
	@echo "=== Port allocation (must not collide with SimCity-3DS-Port) ==="
	@echo "  command protocol: $(SM_DBG_PORT)   (SimCity owns 5002)"
	@echo "  GDB RSP:         $(SM_GDB_PORT)"

setup:
	@echo "1) Fetch the read-only upstream at $(UPSTREAM_REF):"
	@echo "   git clone --recurse-submodules https://github.com/mstan/SuperMetroidRecomp.git $(UPSTREAM)"
	@echo "   cd $(UPSTREAM) && git checkout $(UPSTREAM_REF)"
	@echo
	@echo "2) Pull the devkitPro image (3DS toolchain is already inside it):"
	@echo "   docker pull $(DOCKER_IMAGE)"
	@echo
	@echo "3) Place a legally obtained Super Metroid (Japan, USA) .sfc at:"
	@echo "   $(ROM_PATH)"
	@echo "   expected sha256: $$(grep expected_sha256 $(UPSTREAM)/rom_identity.txt 2>/dev/null | awk '{print $$3}')"
	@echo
	@echo "4) Install Azahar for the headless behavioural gate (see README)."

clean:
	rm -rf build3ds/ objs3ds/ *.nds *.3dsx *.elf