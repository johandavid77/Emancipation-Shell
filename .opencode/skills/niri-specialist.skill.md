---
name: niri-specialist
description: Specialist knowledge for Niri Wayland compositor IPC, event stream, workspaces/windows model, and integration with ext-workspace-v1 bars. Grounded in Noctalia v5 niri backend code.
when: when the user asks about niri, niri IPC, niri workspaces/windows, troubleshooting niri workspace display/order/visibility, or how to align ext-workspace-v1 with niri
---
# Niri Specialist Skill

## Reference sources (authoritative)
Use these files from Noctalia v5 as the source of truth:
- `/home/johan/Descargas/noctalia/src/compositors/niri/niri_runtime.h` (socket, poll/dispatch, reconnect)
- `/home/johan/Descargas/noctalia/src/compositors/niri/niri_runtime.cpp` (UNIX socket discovery, event stream read loop, JSON parsing, dispatchEvent)
- `/home/johan/Descargas/noctalia/src/compositors/niri/niri_event_handler.h` (handler interface)
- `/home/johan/Descargas/noctalia/src/compositors/niri/niri_event_handler.cpp` (registration)
- `/home/johan/Descargas/noctalia/src/compositors/niri/niri_workspace_backend.h` (state, API, event handlers)
- `/home/johan/Descargas/noctalia/src/compositors/niri/niri_workspace_backend.cpp` (parsing, matching, sorting, occupancy, overview)

## Core model
- Runtime maintains event stream over UNIX socket: prefers `$NIRI_SOCKET`, else `$XDG_RUNTIME_DIR/niri-$WAYLAND_DISPLAY.sock`. Connects, sends subscription request, reads newline-delimited JSON events, buffers with size cap, auto-reconnect with exponential backoff. `dispatchEvent(key, value)` fans out to registered handlers.
- Event handler base: `handleEvent(std::string_view key, const nlohmann::json&)`, `handleStreamReset()`.
- Workspace backend tracks: `m_workspaces` map `id(uint64)` → `WorkspaceState {id, idx(uint8), name, output}`; `m_windows`; `m_occupancy` (workspace id → count of windows); `m_focusedWindowId` (optional); `overviewKnown/overviewOpen`. 
- Events handled: `WorkspacesChanged`, `WindowsChanged`, `WindowOpenedOrChanged`, `WindowClosed`, `WindowFocusChanged`, `WindowLayoutsChanged`, `OverviewChanged`.

## Matching & ordering (critical for bar display)
`apply(workspaces, outputName)` maps backend/state into the compositor's workspace list by:
1. Build `candidates = sortedWorkspaceCandidatesForOutput(output)` = workspaces that belong to output (if filtered), sorted by `idx` then `id`.
2. Try to assign each input workspace a candidate match in order: first by parsed numeric `id` equality; else if name non-empty, by exact `name`; else by leading number parsed from `id` or from `name` (as `index`); else leave unmatched.
3. For remaining unmatched (output-scoped), assign next unused candidates in order.
4. After matching: if candidate has `idx>0`, set `workspaces[i].index = candidate.idx`; set `occupied = m_occupancy[candidate.id] > 0`. Unmatched → index 0, occupied false.
`sortedWorkspaceCandidatesForOutput` sorts by `idx < rhs.idx`, else `id < rhs.id`. `workspaceKey(ws)` is derived from workspace identity.

## ext-workspace-v1 alignment (for our C port)
Our implementation uses `ext-workspace-v1` directly (`src/bar/workspaces.c`). To match niri's view:
- ext-workspace-v1 gives groups, handles with `id`, `name`, `coordinates`, `state` (ACTIVE/URGENT/HIDDEN). Ordering is determined by compositor; niri's logical order is reflected in `idx` in its IPC model. The Noctalia backend uses `idx` for ordering.
- When debugging "sometimes 1 vs 2 shown": inspect whether niri has hidden/placeholder workspaces, different outputs, or different coordinate ordering. Check live: `niri msg --json workspaces` and `niri msg --json windows` on test machine.
- Clicking activates via `ext_workspace_handle_v1_activate` + `ext_workspace_manager_v1_commit` (correct). Step prev/next by the ordered snapshot for the target output (no wrap) — matches our `workspaces_step`.
- Occupancy/urgent come from state flags; niri may set URGENT/HIDDEN. Our rendering treats hidden/urgent per state bits.

## Debugging workflow
1. Read the relevant niri backend files for the specific question (cite paths/lines).
2. If live inspection needed, suggest running JSON queries on the niri host (`WAYLAND_DISPLAY`, `NIRI_SOCKET` context).
3. Relate findings back to `workspaces_snapshot`/listeners in `src/bar/workspaces.c` and `bar_draw` in `src/bar/bar.c`.

## Answer constraints
- Ground truth only from the Noctalia niri sources listed above. If absent, say "not found in reference".
- Concise, with exact file paths and key line numbers.
- No code modification instructions unless explicitly requested; analysis only by default.
