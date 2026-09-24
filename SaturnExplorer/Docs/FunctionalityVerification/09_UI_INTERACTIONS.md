# UI Interaction Verification

## Verified headless ImGui behavior
Passing Linux/macOS tests cover:
- row/cell click ownership,
- register context menus,
- vertical row alignment,
- splitters and persisted split height,
- combo sizing,
- SH-2 operand hover targets,
- layer-panel registration/toggles.

## Windows test portability — Failed
Panel interaction tests cannot compile with MSVC because of unconditional `unistd.h`.

## Whole-app UI — Blocked
CI did not interactively verify docking, menus, file dialogs, drag/drop, focus, shortcuts, controller UI, or platform audio in a real application window.
