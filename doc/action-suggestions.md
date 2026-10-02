# Action suggestions

Open **Suggest next actions** from the desktop Actions menu or the light-bulb
icon on the desktop/mobile toolbar (also available in the Android drawer).
The filter icon at the right of the dialog title folds the filters up/down with
an animation, leaving more room for results. Filters start expanded, and closing
them preserves their values.
Choose a state, available time using the 15-minute to 8-hour slider (15-minute
steps), optional categories, and either all eligible
actions or the selected rows in the actions view. An empty category filter includes
uncategorized actions too. The Lists filter offers All, Selected list, and Selected
lists and sublists. It follows the list currently selected in the tree and combines
with category/action-selection filters before ranking and limiting results. With
no list selected, the dropdown is disabled and resets to All. An empty row selection produces no suggestions when
that source is selected.

Only actions with ACTIVE status are eligible. The scope distinguishes actions
with a due date from unset actions (no due date); both includes either group.
Future start dates, completed, deleted, and on-hold actions are excluded.
Exhausted allows trivial/easy difficulty; normal allows through normal; good
through hard; very good through very hard; exceptional includes inspired actions.
Known estimates longer than the available time are excluded. Unknown estimates
remain eligible and display **No estimate**.

The deterministic ranking uses the existing priority/deadline score (85%),
difficulty suitability (10%), and time fit (5%). Unknown estimates receive no
time-fit bonus. Ties sort by title and UUID. **Maximum action suggestions** in
Global Settings limits the final ranked results to 20 by default (range 5–100).
Zero/unset retains that default for older clients. The limit is applied after
filtering and ranking, so it keeps the best matches rather than the first database
rows. Changes synchronize with your other devices and apply on the next search.
Suggestions use the local database,
independent of action-list pagination, and require no AI service.

The compact two-line rows use the same themed surface/text colors as the actions
list. Hover over controls for tooltips; truncated titles and details have tooltips
too. A small selection checkbox is separate from the shared round state/score
control used by the actions list. Click the round control to toggle done/active;
completed suggestions remain visible until the next search so you can undo the
toggle. Work/scheduling controls are disabled while a row is marked done.
The full list location appears on the second line using the actions list's outline
color; due dates and other properties use its secondary text color. On narrow
screens, estimate/difficulty remain available in the title tooltip.
The clock icon (**Start now**) activates a work session. The pen icon
(**Edit**) opens the usual action dialog.
Select checkboxes and choose **Modify selected** to apply category, priority,
difficulty, or schedule changes with the same batch dialog available in the
actions view. Run **Find suggestions** again after editing to refresh the ranking.

Drag the handle onto an existing calendar time box to add the action, or onto
empty calendar space to create a time box. The suggestion dialog leaves calendar
interaction enabled and hides during the native drag, returning
after a drop or cancellation. On small screens it fills the screen; the calendar
icon (**Create time box**)
also supports scheduling without dragging. Drops that would extend beyond the
calendar day are rejected; the explicit creation dialog accepts a start date/time.

The default duration is the estimate, or available time when there is no estimate,
clamped to the global suggestion time-box limits. The defaults are 30–240 minutes.
These settings synchronize through UserGlobalSettings; zero retains the defaults
for older clients. Limits must be 1–1440 minutes with minimum no greater than
maximum. The explicit creation dialog permits adjusting the proposed duration.

## Verification

Runtime tests cover ranking, capacity, time limits, unknown estimates, deadlines,
status/future-start exclusions, category and row selection, empty selections,
settings defaults/custom limits, top-ranked result caps, and time-box creation. Run the desktop
`tst_nextappui_runtime` tests when a build is authorized.

Manual checks:

1. Open from desktop Actions menu/toolbar and Android toolbar/drawer. Check that the
   dialog fills a narrow screen and that its inputs/results scroll independently.
2. Compare priority/deadline ordering at each state; include known and unknown
   estimates, unset dates, future starts, done/on-hold actions, and multiple categories.
3. Select multiple actions in the actions view, restrict the source, and confirm
   only those eligible rows appear. Verify empty selections return no results.
   Check the Lists filter against a nested tree: selected list excludes children,
   selected lists and sublists includes grandchildren, and clearing tree selection
   disables the dropdown and resets it to All.
4. Start a suggestion and confirm it becomes the active work session.
5. Drag onto an existing time box, then onto empty calendar space; confirm the
   action is attached and the created duration uses the global limits. Try a drop
   near midnight that cannot fit. Create a time box with an explicit date/time.
6. Select suggestions and batch change multiple fields. Verify the same dialog
   works from the actions-view selection menu. Edit an individual suggestion.
7. Change global limits, reopen settings, and verify them on another synced device.
8. Check row/text contrast in light and dark themes, compact sizing, and tooltips
   for filters, icons, truncated text, selection, and scheduling. Verify the slider
   endpoints (15 minutes and 8 hours), 15-minute steps, and keyboard adjustment.
9. Toggle the round control done and back to active; verify the server receives
   the same completion requests as the actions list. Check its ring/checkmark
   colors agree with the actions list, and confirm full list paths are readable
   (with tooltips when truncated). Verify selection checkboxes remain separate.
10. During a calendar drag, verify the dialog hides and returns after both
   a successful drop and cancellation (Escape).

The synchronized suggestion limit adds a UserGlobalSettings field with server
validation (zero/unset or 5–100). MCP has no settings/suggestion tools and its
scheduling helpers do not consume this field; no existing MCP schema changes.

## MCP impact review

Suggestion ranking and synchronized time-box limits would be useful to MCP
clients. No MCP tools currently expose suggestions or UserGlobalSettings, so the
new fields do not alter an existing MCP input schema or validation rule. Existing
action/work-session/calendar tools retain their semantics. New MCP exposure is
deferred pending explicit project-owner approval, as required by Agents.md.


## Server support for creation with actions

`CreateTimeblock` accepts an optional action list. It validates ownership and
existence using the same checks as `UpdateTimeblock`, enforces the server's
`time_block_max_actions` setting, and rejects duplicate action IDs. Requests with
attached actions store the protobuf action list and association rows in the same
transaction as the new block. An association-insert failure rolls back the entire
creation; the ADDED notification is queued only after commit. Requests without
actions retain autocommit and a NULL action-list column. Time validation, plan
limits, block kind, and later update/delete behavior are unchanged. No database
migration is needed; deploy a rebuilt server to enable this behavior.

Before deployment, verify against a disposable database/server:

1. Create a block without actions; check NULL actions, no association rows, and
   one ADDED event. Then update/delete it using existing flows.
2. Create blocks with one and several owned actions; fetch/restart/sync and check
   that action order and IDs match both the stored list and association rows,
   including the initial ADDED event. Then update/delete using existing flows.
3. Try missing/foreign-user action IDs mixed with valid IDs; expect INVALID_ACTION
   and no block, links, ADDED event, or consumed plan slot.
4. Try duplicate IDs and one more than `time_block_max_actions`; expect
   CONSTRAINT_FAILED with no writes. Verify exactly the configured maximum succeeds.
5. Inject an association-insert failure after the first valid link; verify all
   block/link writes roll back and no ADDED event or plan-slot consumption remains.
6. Repeat invalid/missing spans, reversed times, cross-day times in the user's
   timezone, read-only access, exhausted quotas, and replay protection checks.

MCP impact: the current MCP tools/help do not expose calendar/time-block creation.
This relaxes an RPC constraint already represented by TimeBlock.actions; the RPC
comment above documents it, and no existing MCP schema or help requires changing.

List filtering is local UI/database behavior; it changes no existing RPC or MCP
schema. The MCP exposure review above still applies.
