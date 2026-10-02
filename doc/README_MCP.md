

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
