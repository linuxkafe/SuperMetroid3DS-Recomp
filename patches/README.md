# The upstream patch chain

**This directory is empty, and that is the intended state.**

## What belongs here

A patch belongs here only when a change to the recompiled core or the snesrecomp
framework **cannot** be expressed in this port's own code:

1. First choice — `frontend/3ds/`. All 3DS-specific behaviour lives there.
2. Second choice — `platform/3ds/`. Replacement for a host-OS boundary.
3. Last resort — a patch here.

If you are reaching for (3) before trying (1) and (2), stop and say why in a
ticket. Most of what feels like "the framework has to change" is really "the
frontend is calling the wrong entry point".

## Naming

`NNNN-short-description.patch`, applied in filename order, zero-padded to four
digits. The first patch in a chain is `0001-`.

## Why this exists at all

The DS attempt had a submodule and no patch chain, and the result is a good
illustration. `PPU_T0_DECL` — a **libnds** macro — was injected directly into the
read-only upstream at `snesrecomp/runner/src/snes/ppu.c`:

```c
 static void PpuDrawBackgrounds(Ppu *ppu, int y, bool sub) {
+  PPU_T0_DECL
```

Two things went wrong, and they are the reason for both halves of this model:

- It cannot compile on anything but DS.
- **It never appeared in a diff of the DS repository**, because the edit lived in
  another tree. Reviewing a port means reading the port's own changes.

That edit is preserved at `legacy/patches/0001-nds-ppu-t0-decl.patch` on the
`ds-legacy` branch. It is evidence of the failure mode, not a patch to apply.

## `make prepare`

Verifies the upstream checkout and reports the chain length. It does not modify
the upstream tree — upstream is read-only and mounted `:ro` in the build.

Upstream is consumed from outside this repository, so a patch cannot be applied
in place. When the chain stops being empty, `make prepare` will apply it to a
writable mirror and point the build at that mirror. Until then it reports zero
patches, which is the honest answer.
