

### Testing MCP interface with curl

```sh

export MCP_URL='<your MCP URL from NextApp Settings/Agent>'
export MCP_TOKEN='<your token from NextApp Settings/Agent>'

codex@codex-ubuntu:~$ curl --fail-with-body -sS "$MCP_URL"     -H "Authorization: Bearer $MCP_TOKEN"     -H 'Content-Type: application/json'     -H 'MCP-Protocol-Version: 2026-07-28'     -H 'MCP-Method: tools/list'     --data '{
      "jsonrpc": "2.0",
      "id": 1,
      "method": "tools/list",
      "params": {
        "_meta": {
          "protocolVersion": "2026-07-28",
          "clientCapabilities": {}
        }
      }
    }'

```
