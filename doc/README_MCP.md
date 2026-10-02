

### Testing MCP interface with curl

```sh

export MCP_URL='<your MCP URL from NextApp Settings/Agent>'
export MCP_TOKEN='<your token from NextApp Settings/Agent>'

curl --fail-with-body -sS "$MCP_URL"     -H "Authorization: Bearer $MCP_TOKEN"     -H 'Content-Type: application/json'     -H 'MCP-Protocol-Version: 2026-07-28'     -H 'MCP-Method: tools/list'     --data '{
      "jsonrpc": "2.0",
      "id": 1,
      "method": "tools/list",
      "params": {
        "_meta": {
          "io.modelcontextprotocol/protocolVersion": "2026-07-28",
          "io.modelcontextprotocol/clientCapabilities": {}
        }
      }
    }'

```

### On-demand MCP help

`get_mcp_help` is a read-only tool served by the running application. It uses the
same authentication and online/enabled requirements as other MCP tools and
requires no external documentation service.

Call it using `tools/call`, for example:

```json
{
  "name": "get_mcp_help",
  "arguments": {"subject": "tool", "name": "nextapp_update_action"}
}
```

Use exact registered tool names (including `nextapp_`). Shared schemas are
`action`, `node`, and `category`; these describe the MCP JSON projections, rather
than the whole protobuf model. Concepts are `mutation`, `node_kind`,
`action_status`, `action_priority`, `timestamps`, `node_relationships`, and
`pagination`. An unknown subject/name produces a tool error with available names.

Help returns structured JSON in `structuredContent`, with a matching JSON text
content block for clients that consume text. Tool help includes its registered
input schema, field semantics, examples, side effects, common errors, and links
to shared subjects. Every document includes the running application version.
Examples use placeholder UUIDs: replace them with IDs obtained from reads.

Mutation validation errors use the existing `isError` tool-result envelope. Field
errors include `error`, `field`, `message`, `constraints`, and a help reference;
invalid enum values also include `allowed`. Invalid fields are rejected before
idempotency reservation or approval. Domain validation, approval gates, version
checks, and backend validation still apply. Retry and uncertain-outcome rules are
documented in `concept/mutation`.

Desktop tests are `tst_nextappui_mcp` (metadata, help, validation, and drift checks)
and the MCP gateway case in `tst_nextappui_runtime`. They are enabled with
`NEXTAPP_WITH_MCP` and `NEXTAPP_WITH_TESTS_UI`.

### Simple structured action creation

Use `nextapp_add_action_simple` for quick additions. The agent supplies structured
fields; NextApp normalizes them into an Action protobuf and submits it through
`addActionDirect`. This tool uses the existing **createAction** approval setting,
request audit, and idempotency handling. Approval displays the normalized fields.
The result includes `normalizedAction` in protobuf JSON form.

```json
{
  "name": "nextapp_add_action_simple",
  "arguments": {
    "idempotencyKey": "darkspeak-encryption-release-001",
    "text": "Refactor the code in darkspeak that handles encryption and also fix the UI issues.",
    "schedule": "next_week",
    "tags": ["release", "beta", "verify"],
    "priority": "high"
  }
}
```

Only `idempotencyKey` and `text` are required. Omit `topic` to derive a title with
the clipboard paste rules and configured title word count. The body becomes the
description, bounded at a complete UTF-8 character to the paste description limit.
Omit `nodeId` for Inbox; supply an existing node UUID to choose another list.
The text is the action body, not an instruction string: agents must extract
scheduling, tags, priority, repetition, and other options into their own fields.

Scheduling uses the same functions as the UI, including configured timezone and
week start. Supported examples:

```json
{"schedule": "next quarter"}
{"schedule": "2027-02-03"}
{"schedule": "2027-02-03T10:15:00+02:00"}
{"schedule": "week #42 2027"}
{"schedule": "month November 2027"}
{"schedule": {"kind": "quarter", "value": 2, "year": 2027}}
{"schedule": {"kind": "year", "value": 2027}}
```

Shortcut spellings tolerate case, spaces, and hyphens, including `next quater`.
A missing period year means the current year (ISO week-year for week numbers).
Week numbers are ISO; the resulting date range follows the configured UI week
start. An offset-free datetime uses the configured timezone; an explicit offset
preserves its instant. Omitted scheduling leaves the action unscheduled.

Repetition stays structured and maps to the UI/backend recurrence fields:

```json
{"repeat": {"from": "completed", "every": 2, "unit": "weeks"}}
{"repeat": {"from": "due_time", "on": ["monday", "friday", "last_day_in_month"]}}
```

The default repeat origin is completion, interval is 1, and unit is days.
`on` selects UI weekday/period-boundary specifications and cannot be mixed with
`every` or `unit`. Repeating actions with omitted scheduling start today; explicit
`unscheduled` recurrence is rejected. Repetition is performed by the backend when
the action is completed. `repeat: "never"` disables it.

Other fields are `difficulty`, `timeEstimate`, `favorite`, `category` or
`categoryId`, and `reason`. Estimates accept integer minutes or UI `H:MM` /
`D:H:MM` strings (8-hour workdays). Tags accept an array or a separated string and
use the UI tag parser. Categories must exist; names match exactly without case,
and ambiguous names return candidate UUIDs. Unknown properties and unsupported
values return structured errors before approval or backend submission.

Fetch `get_mcp_help` with `subject: "tool"` and
`name: "nextapp_add_action_simple"` for all field semantics, constraints,
protobuf enum values, recurrence day specifications, and examples. Reuse the
same idempotency key and identical input on retry: the original outcome is
returned even after date, timezone, or Inbox changes.
