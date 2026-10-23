# Investigation report

Date: 2026-10-20. Inspected source: `ff6a3b1fb1c54e4c0344613094ea65292b0023c2`, master.
Scope: Saturn Explorer inside NoahSteam/RomHackingTools. Documentation-only investigation.

## Coverage

The survey began with CLAUDE.md, repository/application overviews, architecture/build documents,
public ABI headers and CMake/test registrations. Independent source investigations covered
portable graphics; drivers/tap integration; debugger/memory/history; decompiler; disc/search/
patching; and AI investigation. Coordination checked App/platform orchestration and cross-system
ownership, capture, command dispatch, worker/result and export ordering.

The knowledge base includes seven requested top-level navigation/reference documents, this
report and eight focused subsystem pages. FEATURE_MAP organizes the major implemented behavior
and developer tasks by functionality, including the explicitly unimplemented paused-variable
feature and AI workflow boundary. Source links and symbols identify implementation entry points;
test references distinguish existing coverage from fresh execution.

Deepest traced paths: hardware capture→derive→render; live transport→pinned capture→displayed
stop; command policy→historical-view selection→exports; debugger step/transient/halt ownership;
owned decompiler request→worker→identity-checked result; recorder/state-block reconstruction and
staged edits; file search→accepted mapping→patch script; disc rebuild publication; M0 evidence→
response validation→preview. See DATA_FLOW and subsystem pages for the actual symbols.

## Validation performed

- Checked 493 local Markdown targets, including GitHub-style heading anchors, throughout the
  knowledge base and the added instruction links; all resolved. Also checked 23 explicitly
  qualified inline source paths and the feature-map test file names.
- Checked important named symbols against source and used call-site inspection for the principal
  cross-system paths. Each subsystem investigator verified its source/test references.
- Reviewed feature-map/subsystem coverage and distinguished implementation, design intent and
  uninvestigated behavior. Known stale older descriptions are catalogued without overwriting them.
- Confirmed all content changes are under `Docs/Codebase/` plus the minimal CLAUDE.md workflow
  addition. No production source, test or build configuration changes are part of this work.
- Cross-reviewed the coordinating documents against the independent subsystem investigations,
  correcting function-discovery symbols, recorder-thread ownership and restore-timeout wording.
- Checked whitespace with `git diff --check`. No application build or tests were run for this
  documentation-only change; cited tests are existing fixtures/registrations inspected in source.

## Unresolved questions and deeper checks

The map does not establish native UI/packaging parity, real-emulator restore correctness, all
hardware equations or full vendored-engine correctness. Update-checker shutdown/platform lifetime
needs a separate runtime/lifecycle review before treating detached requests as proven safe.
Installer scripts and emulator accessors were surveyed at their boundaries rather than fully
audited. Existing tests and review ledgers are navigation/oracles, not fresh correctness sign-off.
Other tools in the containing repository remain outside scope.

Confirmed constraints and documentation discrepancies appear in [KNOWN_LIMITATIONS](KNOWN_LIMITATIONS.md).
The most useful deeper investigations depend on the next task: semantic storage/provenance for
pseudocode variable values, real-game rendering captures for hardware work, or actual emulator
protocol/restore runs for execution changes. These are suggested investigation routes, not claims
that corresponding production defects exist.

## Maintenance and delivery

[CLAUDE.md](../../CLAUDE.md) now directs agents to README, FEATURE_MAP and relevant subsystem
pages before inspecting source and making changes. It requires updates in the same change for
material behavior/API/architecture/flow changes while exempting insignificant local details.
Existing architecture/build/hardware/review documents remain intact and linked.

The repository's current agent instructions require direct master commits/pushes for ordinary
work; this documentation task follows that workflow. Source metadata records the inspected
parent revision rather than recursively naming the documentation commit that contains it.
