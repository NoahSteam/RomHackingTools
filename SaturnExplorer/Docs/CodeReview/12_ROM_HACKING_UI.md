# 12 — ROM-Hacking Workflow / Frontend

## ROM-01 — High — generated patch script does not verify baseline target bytes

`PatchLocation.expected` stores the original baseline, but the generated Python script contains only path, offset, and replacement bytes. It never reads the target span and verifies it still matches `expected`.

A project used against another game revision can silently write at a stale offset.

**Fix:** include expected bytes in each patch entry and refuse to write unless the target bytes match. A force option should be explicit.

## ROM-02 — Medium — project paths can escape the patch directory

Loaded project paths can contain absolute paths or `..`. The generated script joins them to BASE without containment checks.

**Fix:** normalize and reject escaping paths both when loading the project and in the generated script.

## ROM-03 — Medium — recursive search can follow directory-link cycles

POSIX uses `stat`, Windows does not reject reparse-point directories, and there is no visited-set/depth cap in DataSearch recursion.

**Fix:** do not follow directory links by default, or track visited identities and cap depth.

## ROM-04 — Medium — PRS search has pathological CPU complexity

It intentionally attempts decompression at every byte offset. Size/cancel limits help, but adversarial or highly compressible inputs can remain very expensive.

**Fix:** add candidate filtering and a per-file work/time budget.

## ROM-05 — Low — patch text format does not enforce delimiter/schema constraints

Serialize does not reject tabs/newlines, and deserialize does not require `expected.size() == length`.

**Fix:** validate strictly or move to a versioned JSON/binary schema.

## UI-01 — Medium — `App.cpp` has become an oversized lifecycle coordinator

It now owns source lifecycle, debugger, tracepoints, rewind, disc, patch, audio, launching, demos, settings, and UI integration.

**Fix direction:** extract `LiveSession`, `DebugSession`, `RewindController`, `PatchProject`, `DiscWorkspace`, and `AudioWorkspace`.

## UI-02 — Low — architecture documentation is behind implementation

`ARCHITECTURE.md` still describes early milestones and outdated subsystem locations.

Update it after corrective work.
