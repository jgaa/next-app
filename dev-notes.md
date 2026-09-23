# Development notes

## MCP request-scoped SSE

Add SSE after ordinary MCP HTTP requests are stable in real clients. Qt HTTP Server 6.10 already supports chunked responses through `QHttpServerResponder::writeBeginChunked()`, `writeChunk()`, and `writeEndChunked()`. A pending approval could use `text/event-stream` and send its final JSON-RPC response on that request's stream.

Cancellation needs separate work. MCP requires a closed request-scoped SSE stream to cancel the pending operation, but Qt 6.10 has no public responder API to detect that disconnect. Qt 6.11 adds `QHttpServerResponder::isResponseCanceled()`. Before enabling SSE, either provide a reliable Qt 6.10-compatible disconnect mechanism or require Qt 6.11 for MCP. Keep the operation, approval, and HTTP response lifetimes tied together, and test client disconnects before and after mutation submission.

References: [Qt 6.10 responder](https://doc.qt.io/qt-6.10/qhttpserverresponder.html), [Qt 6.11 responder](https://doc.qt.io/qt-6.11/qhttpserverresponder.html), [MCP Streamable HTTP](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/streamable-http).
