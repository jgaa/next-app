# NextApp Agent Support Two Phase Design

## Status

Proposed design for experimentation and later production implementation.

## Motivation

NextApp should allow users to bring their own agent and language model. The agent may use a local model, a remote model, or no language model at all. NextApp provides structured context and operations; it does not provide or manage the agent's reasoning loop.

The immediate goal is to get a useful implementation running quickly so the design can be tested with real agents before committing to the complete distributed architecture. The first phase therefore supports one local NextApp UI instance per agent and asks for confirmation in that UI. The second phase adds durable server-side approvals, approval from any active device, and a scaled-down headless NextApp client suitable for containers.

The final architectural rule is:

> Agents are NextApp clients, not privileged backend actors.

Agents must never receive direct database access, unrestricted query access, or a special path to expensive server operations. Reads primarily use locally synchronized state. Mutations use the same validated client operations and normal synchronization path as interactive changes.

## Goals

- Let users connect an agent through MCP or another adapter with equivalent semantics.
- Support local and remote language models without coupling NextApp to a model vendor or runtime.
- Expose useful, bounded, semantic operations rather than database primitives.
- Let an agent propose and perform operations without trusting its prompts, model, or external inputs.
- Require deterministic authorization outside the model.
- Preserve provenance and an audit trail for agent requests and decisions.
- Keep server cost bounded by making ordinary reads local.
- Allow the prototype to evolve into the full implementation without changing the fundamental agent-facing concepts.

## Non Goals

- Building an agent framework or reasoning loop into NextApp.
- Giving an agent SQL access or arbitrary backend query facilities.
- Sending credentials for integrations through an LLM.
- Uploading prompts, chain-of-thought, or full agent conversations to nextappd.
- Supporting unattended consequential operations in phase 1 unless the user has explicitly
  selected `Always allow` for that specific mutation type and agent scope.
- Implementing general distributed approvals in phase 1.
- Guaranteeing that the phase 1 persistence format is compatible with phase 2.

## Common Security Model

Every agent must be treated as potentially compromised. This includes local agents: their model may process hostile email, calendar entries, issue descriptions, documents, or other prompt-injection content.

Security must therefore depend on:

- a distinct agent identity;
- explicitly granted capabilities;
- validation of each concrete operation and its arguments;
- human approval where required;
- bounded queries and responses;
- idempotent mutations;
- audit and provenance;
- the existing NextApp validation and synchronization path.

Advertising an MCP tool does not authorize every possible invocation of that tool. Authorization occurs after NextApp has received and validated the concrete arguments.

## Shared Agent Interface

The two phases should expose the same broad categories of agent operations, although phase 1 may implement only a subset.

### Read operations

Examples include:

- `get_inbox`
- `get_today`
- `get_list`
- `get_list_actions`
- `get_action`
- `search_actions`
- `get_recent_changes`

Reads must use semantic APIs with pagination, result limits, response-size limits, timeouts, and concurrency limits. MCP must not expose SQL or arbitrary server filters.

### Suggestion operations

Examples include:

- `suggest_action`
- `suggest_update`
- `suggest_completion`

A suggestion is not an action mutation. It is a first-class proposal that the user can accept, modify, reject, or ignore. Suggestions are a likely later addition, but are not part of the first phase 1 implementation.

### Mutation operations

The initial useful set is:

- `create_action`
- `update_action`
- `complete_action`

Deletion, project-wide changes, bulk operations, and external side effects are excluded from phase 1 and should be added cautiously in phase 2 or later.

Every mutating request includes an agent-generated idempotency key. In phase 1, the client stores the agent identity, idempotency key, normalized full request, a cryptographic hash of that request, lifecycle state, and result in the local database.

The pair `(agent identity, idempotency key)` is unique:

- the same key and same request hash returns the existing pending state or recorded terminal result;
- the same key with a different request hash is rejected as an idempotency conflict;
- reserving the key and request record is atomic and happens before approval is requested;
- a request whose server outcome is uncertain is not automatically replayed.

These records and the local audit records are local-only data. A full synchronization must preserve them when replacing the synchronized database, as described below.

### Agent identity and capabilities

Each configured agent has:

- a stable identifier;
- a user-facing name;
- an installation or client identity;
- granted read, suggestion, and mutation capabilities;
- a per-mutation gate with one of `Disabled`, `Ask`, or `Always allow`;
- optional scope restrictions, such as selected projects;
- enabled or disabled state.

An agent's stated name, reason, and explanation are untrusted text. NextApp controls the rendering of the operation type, target, changed fields, and approval controls.

# Phase 1 Local Experimental Implementation

## Purpose

Phase 1 exists to learn how real agents use NextApp. It should be small enough to implement and revise quickly. It does not need to solve offline approval, cross-device approval, headless operation, or durable server coordination.

The expected deployment is one agent connected to one running NextApp UI instance. Multiple agents may be configured eventually, but each agent connection is owned by one UI instance and all confirmations for that connection appear there.

## Architecture

```mermaid
flowchart TD
    A["User supplied agent"] -->|MCP| M["MCP adapter"]
    M --> G["Agent gateway"]
    G --> R["Local read services"]
    G --> P["Local policy and approval"]
    P --> U["NextApp UI"]
    P --> C["Existing client mutation API"]
    R --> D["Local SQLite cache"]
    C --> S["Normal NextApp synchronization"]
```

The MCP adapter and agent gateway run inside the desktop NextApp process.

The gateway must depend on abstract read, mutation, audit, and approval interfaces so it can later be reused by the headless client.

## Transport

Phase 1 uses MCP Streamable HTTP, with optional HTTPS. It does not support stdio.

The transport must:

- bind only to loopback by default;
- require an unguessable per-agent credential on every connection;
- validate the HTTP `Origin` header and reject an invalid origin;
- never expose the endpoint on all interfaces by default;
- place strict request, connection, header, and body-size limits on it;
- use TLS when configured on a non-loopback interface;
- clearly show when the endpoint is enabled;
- stop accepting requests when AI or MCP is disabled, while offline, or during synchronization.

Credential creation, storage, rotation, and migration to an operating-system credential vault are tracked separately. Until vault support exists, credentials must at minimum be stored with restrictive local file permissions and must never appear in logs, command-line arguments, audit records, or MCP results.

### Native Phase 1 MCP implementation

Phase 1 does not depend on a general-purpose MCP SDK. It uses Qt HTTP Server for HTTP request handling and an intentionally small NextApp-native MCP layer implemented with Qt JSON types and the existing QCoro-based services.

The initial implementation is pinned to the current MCP protocol revision, `2026-07-28`. Additional revisions are added only when required for interoperability with a target client. This revision is stateless and carries protocol version, client capabilities, and optional client information on every request, so Phase 1 does not implement the legacy initialization handshake or protocol-level sessions.

The native layer supports only the protocol needed by NextApp:

- JSON-RPC 2.0 requests, responses, and errors;
- the required per-request `_meta` fields and corresponding Streamable HTTP headers;
- exact validation that the mirrored protocol version, method, and tool name headers match the JSON body;
- `tools/list`, in deterministic order and with a bounded cursor if pagination is needed;
- `tools/call`;
- complete structured tool results, with the same result serialized as text for compatibility;
- an ordinary JSON response for short requests;
- a request-scoped SSE response for a mutation waiting on validation or user approval, followed by its final JSON-RPC response;
- cancellation of an outstanding streamed request when the client closes that request's SSE response stream.

Phase 1 does not advertise or implement MCP prompts, resources, subscriptions, sampling, elicitation, completions, logging, tasks, multi-round-trip input, server-initiated requests, or list-changed notifications. It does not implement a standalone GET stream, resumable streams, or MCP session IDs. HTTP GET and DELETE on the MCP endpoint return `405 Method Not Allowed`. Unsupported JSON-RPC methods return the standard method-not-found error with the HTTP status required by the pinned MCP revision.

The MCP parser must reject invalid JSON-RPC request shapes, invalid identifiers, batch payloads, missing or unsupported per-request protocol metadata, header/body mismatches, and oversized messages. Tool input is converted into typed domain requests and validated by the gateway; the transport layer must not forward arbitrary JSON into SQL or protobuf reflection. NextApp tool schemas use JSON Schema 2020-12 and do not use remote `$ref` values.

Qt HTTP Server supplies HTTP parsing, routing, connection handling, chunked responses, and optional TLS. The NextApp layer remains responsible for authentication, Origin validation, request metadata, JSON-RPC and MCP semantics, rate and size limits, cancellation, and redaction. Phase 1 must not expose Qt HTTP Server directly to the public internet; a non-loopback listener is limited to an explicitly trusted private network and requires HTTPS.

The CMake MCP option controls the Qt HTTP Server dependency so builds without MCP do not require or link that module. Protocol tests should cover per-request metadata and header matching, every implemented method, malformed JSON-RPC, authentication and Origin rejection, JSON and SSE responses, cancellation, timeouts, size limits, and representative interoperability exchanges with target MCP clients.

Qt 6.11 adds `QHttpServerResponder::isResponseCanceled()`, while NextApp currently supports Qt 6.10. Before implementation, verify a public Qt 6.10 mechanism for observing a closed request-scoped SSE stream. If none is reliable, either add a small compatible connection-cancellation shim or require Qt 6.11 only when MCP is enabled; do not silently ignore MCP cancellation.

## Implementation details

Requests should use existing internal methods when possible.

All requests must use async coroutines via QCoro.

Phase 1 MCP mutations do not use the client's durable queue-and-execute path. After approval and validation, the gateway awaits the ordinary gRPC mutation directly and records the returned status. This keeps the experimental lifecycle correlated with the open MCP operation and prevents an approved operation from being silently replayed after reconnect.

Client-side idempotency suppresses repeated MCP calls, but it cannot prove the result of a gRPC call whose connection fails after the server may have committed it. Such a request is recorded with an uncertain server outcome and is never retried automatically. The MCP result must tell the agent that manual reconciliation is required.

The MCP feature is implemented as a separate static library.

## Reads

All phase 1 reads operate on the local SQLite cache or existing in-memory client models. An MCP read must not trigger a new unbounded backend query. If synchronized state is incomplete, the response should report that fact rather than bypassing the client architecture.

Minimum limits must include:

- maximum page size;
- maximum serialized response size;
- maximum concurrent calls per agent;
- query deadline;
- rate limit;
- maximum search length and complexity.

Read responses should include stable UUIDs and enough revision or modification information for an agent to avoid acting on stale assumptions.

Every read response should also include cache completeness, online state, the last successful synchronization time or watermark, and whether synchronization is pending or active. Pagination must use deterministic ordering and bounded cursors rather than an unbounded offset over changing data.

## Local confirmation flow

For phase 1, each mutation type has a user-controlled gate:

- `Disabled` rejects the tool call;
- `Ask` requires local confirmation for each new idempotent request;
- `Always allow` skips the confirmation dialog but still performs all authentication, scope, validation, timeout, idempotency, and audit steps.

The confirmation dialog for an `Ask` operation may offer an unchecked `Always allow` checkbox. Selecting it changes only that mutation type for that agent and its displayed scope. The label must name the exact operation and scope; it must not create a broad or implicit permission.

1. The MCP adapter receives a structured tool invocation.
2. The gateway authenticates the configured agent and checks its capability.
3. NextApp performs structural and semantic validation in an awaited coroutine with a deadline.
4. The gateway constructs an immutable local pending operation.
5. If the mutation gate is `Ask`, QML displays a confirmation generated from the structured operation. If the gate is `Always allow`, the workflow proceeds without a dialog.
6. For an `Ask` operation, the user approves or rejects it in the same running UI.
7. Before execution, the gateway revalidates the operation against current local state using the same awaited validation path and deadline.
8. The existing client mutation API performs a direct awaited gRPC call, without queue-and-execute, and normal synchronization observes the resulting update.
9. The result is returned to the MCP caller and written to the local audit log.

A validation timeout or cancellation is a failure, never an implicit approval. The approved operation is bound to its normalized request hash and the base revision shown in the dialog. If relevant local state changes before execution, the request fails and the agent must submit a new request for a new approval.

The first update tools use patch semantics: `update_action` contains only explicitly changed fields, and `complete_action` changes only completion state. Both include the base action version in the request and audit record. Immediately before calling the existing full-object update RPC, the client reloads the current local action, verifies its version, and applies only the approved patch to that object. Phase 1 therefore does not construct a full update from stale agent-supplied data. A later server change will enforce the expected version authoritatively; until then, the MCP response and activity history must identify that only client-side stale-write protection was available.

The dialog must show:

- agent name;
- operation type;
- target object and current user-visible name;
- exact proposed field changes;
- agent-supplied reason, clearly identified as untrusted explanatory text;
- Approve and Reject actions.

The dialog must not render arbitrary rich text, remote images, active links, or agent-supplied UI markup.

## Waiting behavior

Phase 1 may keep the MCP tool call open while the confirmation dialog is pending, subject to a short configurable timeout. This is intentionally a prototype simplification.

For a mutation using a request-scoped SSE response, the client closing that response stream cancels the associated MCP operation. Loss of an unrelated or idle HTTP connection has no effect because the protocol is stateless. UI exit, disabling AI or MCP, operation timeout, transition offline, or synchronization start also cancels requests that have not been submitted to gRPC. Pending requests do not resume after a client restart in phase 1.

When the client goes offline, all MCP operations are cancelled and their records receive `ABORTED_OFFLINE`. If a direct gRPC mutation had already been submitted, its server outcome may be marked unknown in addition to `ABORTED_OFFLINE`; it is never replayed automatically when the client reconnects.

The internal request object should nevertheless have a UUID, timestamps, operation type, target, arguments, idempotency key, agent identity, and state. This provides useful experience for the phase 2 data model.

## Phase 1 state model

```mermaid
stateDiagram-v2
    [*] --> Validating
    Validating --> Pending: valid and Ask
    Validating --> Approved: valid and Always allow
    Validating --> Failed: invalid or deadline
    Pending --> Approved: user approves
    Pending --> Rejected: user rejects
    Pending --> Cancelled: response stream closed or UI exit
    Pending --> AbortedSync: synchronization starts
    Pending --> AbortedOffline: client goes offline
    Pending --> Expired: timeout
    Approved --> Executing: revalidation succeeds
    Approved --> Failed: validation fails or times out
    Executing --> Executed: gRPC mutation succeeds
    Executing --> Failed: authoritative rejection
    Executing --> AbortedOffline: client goes offline
    Executing --> OutcomeUnknown: transport lost after submission
```

Only `Pending` may receive a user decision. Resolution must be atomic within the client so repeated clicks or duplicate MCP calls cannot execute the operation twice. An executing call with an uncertain transport result remains terminal and is not automatically retried.

## Local audit and provenance

Phase 1 records locally:

- request ID and idempotency key;
- agent identity;
- operation type and normalized arguments;
- target UUID;
- creation and resolution times;
- approval decision;
- executing user and device identity where available;
- result or error;
- resulting object UUID and revision where available.

Do not record prompts, hidden reasoning, credentials, or unrelated model context.

Where practical, objects created or changed by an agent should retain lightweight provenance that can later be shown as “Created through Planning Agent.” Full server-visible provenance is a phase 2 concern.

## Synchronization and offline transitions

Phase 1 does not attempt to reconcile MCP operations across synchronization or connectivity transitions.

Before a full synchronization starts, NextApp must:

1. suspend the MCP HTTP listener and stop accepting new MCP requests without changing the user's configured enablement;
2. cancel validation and approval requests that have not submitted a mutation;
3. cancel in-flight MCP work and wait until all MCP operation coroutines have finished;
4. show `Waiting for MCP ops` in the synchronization progress only if synchronization actually had to wait;
5. record `ABORTED_SYNC` or the other appropriate terminal result before replacing the database;
6. copy the MCP request, idempotency, and audit tables into the staged database in the same transaction used to finalize that database;
7. commit and swap the staged database only after the local-only records have been copied successfully.

MCP can resume after synchronization only when AI and MCP remain enabled and the client is online. Failure to copy the local-only tables aborts the database swap rather than silently discarding idempotency or audit state.

When the client detects that it is offline, it stops accepting new MCP work, cancels all current MCP operations, stores `ABORTED_OFFLINE` as the request result, and does not replay those operations after reconnect. A request already submitted to gRPC additionally records whether the server outcome is unknown.

## Phase 1 resource defaults

All limits are configurable through QSettings keys under `ai/mcp/limits/`. The first implementation does not require UI controls for these values. Invalid or unsafe configuration values must fall back to bounded defaults.

| Limit | Default |
| --- | ---: |
| Concurrent MCP calls | 4 |
| Concurrent executing mutations | 1 |
| Pending approval requests | 8 |
| HTTP connections | 8 |
| HTTP request body | 256 KiB |
| Maximum page size | 50 objects |
| Maximum serialized tool result | 512 KiB |
| Read/validation deadline | 5 seconds |
| Approval timeout | 2 minutes |
| Calls per minute per agent | 60 |
| Mutation requests per minute per agent | 10 |
| Search text | 256 Unicode characters |
| Agent reason | 2 KiB UTF-8 |
| Retained idempotency/audit records | 10,000 |
| Retention time | 30 days |

Reaching a limit returns a structured, non-retryable or retry-after error as appropriate. Fixed semantic queries must remain independently bounded even when configuration values are raised.

## Phase 1 UI

The minimum UI consists of:

- a master toggle in global settings to "Enable AI", default off. It takes precedence over every individual agent setting. If off, no other AI-related UI elements are visible and all AI features are disabled, including the MCP HTTP listener even if it was configured.
- an Agent settings page for enabling the local MCP interface, managing one agent identity, and selecting the `Disabled`, `Ask`, or `Always allow` gate for each supported mutation;
- a confirmation dialog or queue;
- a small local activity history suitable for debugging.
- a suitable icon close to the online icon on the main screen showing agent activity through color/animation

The UI should make it easy to disable agent access immediately.

## Phase 1 implementation boundaries

To keep the experiment fast, phase 1 deliberately omits:

- server protocol changes for approvals;
- approval from another device;
- persisted pending operations across restarts;
- headless or container execution;
- broad autonomous permissions beyond explicit per-agent, per-operation `Always allow` gates;
- agent access to external-service credentials;
- bulk and destructive operations;
- suggestions in the first implementation;
- a complete long-term policy language.

However, phase 1 must not bypass the existing mutation API, write directly to SQLite, expose arbitrary queries, or place approval logic directly in QML. Those shortcuts would invalidate the experiment and make migration harder.

## Server changes identified during phase 1

The client implementation should maintain a concrete backlog of server changes discovered during the experiment. Minor compatible server changes may be delivered with the feature. At minimum, the known backlog contains:

- add an authoritative expected-version precondition for action updates;
- add an authoritative expected-version precondition for completion changes;
- add patch-oriented action mutation RPCs or field-mask semantics so clients do not need to send a full object;
- accept a stable client request identifier suitable for end-to-end idempotency and uncertain-result reconciliation;
- return enough resulting object and revision data for a directly awaited mutation to produce an exact MCP result;
- add durable agent provenance when the phase 2 audit model is introduced.

Until the expected-version changes are deployed, Phase 1's stale-write checks are client-side only and are intended primarily for experimentation against the staging server.

## Phase 1 success criteria

Phase 1 is successful when it can answer these questions with real use:

- Which NextApp resources and tools do agents actually need?
- Are the tool schemas understandable to both small local models and hosted agents?
- Which operations become annoying when confirmed every time?
- What information makes confirmation dialogs understandable and safe?
- What query and response limits work without making agents ineffective?
- How often do agents retry or duplicate requests?
- What audit information is useful in practice?

Phase 1 remains behind a CMAKE option for MCP and a CMAKE option for AI in general, both enabled by default.

# Phase 2 Full Distributed Implementation

## Purpose

Phase 2 turns local confirmation into a general, durable NextApp approval mechanism. It supports headless agent clients, approvals from any authenticated active device, recovery after disconnection or server restart, and delayed delivery to devices and agents that reconnect later.

The mechanism should be general enough for future non-agent approvals, but agent requests are its first use case.

## Architecture

```mermaid
flowchart TD
    A["User supplied agent"] -->|MCP| H["Headless NextApp client"]
    H -->|normal request with approval metadata| S["nextappd"]
    S -->|pending approval update| D["Authenticated NextApp devices"]
    D -->|approve or reject| S
    S -->|validated result update| H
    H -->|correlated result| A
```

The headless client contains a scaled-down form of the ordinary NextApp client:

- authentication and device identity;
- synchronization protocol;
- local SQLite cache;
- shared domain models and validation helpers;
- agent gateway and MCP adapter;
- policy and resource limits;
- no QML or interactive desktop dependencies.

The container runs in the user's infrastructure. It pays the compute cost of agent reads and model use. nextappd sees bounded normal synchronization traffic plus the approval lifecycle.

## Deferred transactional updates

An approval request is an ordinary mutation request plus approval metadata. If approval is required, nextappd validates what it can immediately, stores the exact immutable request, and does not execute it yet.

Approval authorizes nextappd to attempt that stored operation against current authoritative state. It does not authorize the client to submit a later modified operation.

This avoids a request-approval-request substitution problem and gives the system durable asynchronous semantics.

## Submission validation

Before persisting a pending request, nextappd rejects requests with:

- invalid or oversized protocol data;
- unknown operations;
- invalid authentication or device identity;
- missing agent/request identity;
- capabilities that do not allow requesting the operation;
- malformed identifiers or impossible static arguments;
- expired request timestamps;
- duplicate idempotency keys with conflicting payloads;
- requests exceeding tenant, device, or agent limits.

State-dependent validation that can change while approval is pending is repeated at execution time.

## Suggested pending request model

The exact protobuf and database schema should follow established NextApp conventions, but the logical record needs:

| Field | Purpose |
| --- | --- |
| Request ID | Stable correlation identifier |
| Tenant and user | Ownership and authorization scope |
| Requesting device | Headless or interactive client identity |
| Agent identity | User-visible agent provenance |
| Idempotency key | Duplicate suppression |
| Operation type | Semantic mutation being requested |
| Immutable payload | Exact normal request to execute |
| Normalized summary | Server-generated approval presentation data |
| Agent reason | Optional untrusted explanation |
| Created and expiry times | Lifecycle and cleanup |
| State | Pending, executing, or terminal state |
| Resolution identity | User and device that decided |
| Resolution and execution times | Audit and ordering |
| Result or error | Durable outcome for reconnecting clients |
| Resulting revision | Correlation with synchronized state |

The immutable payload and all security-relevant metadata must be protected against modification after submission.

## Distributed lifecycle

```mermaid
stateDiagram-v2
    [*] --> Pending
    Pending --> Executing: first valid approval
    Pending --> Rejected: first valid rejection
    Pending --> Expired: deadline
    Pending --> Cancelled: requester cancels
    Executing --> Executed: transaction succeeds
    Executing --> Failed: revalidation or execution fails
```

The transition out of `Pending` must be transactional. If two devices answer concurrently, the first valid transition wins. All other devices receive the resolved state and remove the pending prompt.

`Executing` prevents duplicate execution after a race or retry. Recovery logic must safely resume or resolve requests left in that state after a server restart. Prefer executing the state transition and mutation within one database transaction where the affected operation permits it. Where that is impossible, the implementation must define idempotent recovery semantics before enabling the operation.

## Execution after approval

After approval, nextappd loads the stored request and revalidates it against current database state using the same authoritative validation path as an immediate normal update.

Approval means “attempt this exact operation now,” not “force this outcome.” The operation may fail because the target was deleted, completed, moved, changed, or no longer satisfies a precondition.

An approved request must not bypass normal business rules, authorization, optimistic concurrency checks, or tenant isolation.

## Replication and delivery

Pending approvals and retained terminal results are synchronized state, not transient notifications.

nextappd sends:

- a pending-request-added update to eligible active sessions;
- all still-pending requests during initial synchronization or reconnection;
- a pending-request-resolved update to all relevant sessions;
- durable terminal results to the requesting client when it reconnects.

The existing ordered update and resynchronization mechanisms should be used where possible. Delivery must not depend on an RPC remaining open.

The requesting MCP operation becomes asynchronous in phase 2. It returns a stable NextApp request ID and an initial status. The agent can receive completion through the MCP mechanism selected during implementation or poll a bounded `get_request_status` tool. The design must not assume that a specific agent remains connected for the lifetime of the request.

## Approval UI

Any authenticated eligible NextApp device may show a pending approval. The UI is built from normalized structured data, not from agent-generated markup.

The approval view shows:

- requesting agent and device;
- operation and affected object;
- current object state where relevant;
- exact proposed changes;
- creation and expiry times;
- optional agent-supplied reason;
- Approve and Reject actions.

The UI must refresh or clearly identify stale displayed state before a decision. Server-side revalidation remains mandatory even if the UI refreshes.

Android and desktop may use notifications to announce a request, but the decision must occur in authenticated NextApp UI. Notification actions should only be used if they can provide equivalent authenticated, deliberate confirmation without leaking sensitive details on the lock screen.

## Eligibility and policy

Initially, any active authenticated device for the same user may decide a request. Tenant and role rules must be checked server-side.

Phase 2 must migrate or re-confirm Phase 1 `Always allow` gates as server-enforced policy. It must not silently broaden an operation, agent, or project scope during migration. The server, rather than the requesting agent, becomes authoritative for those gates and their revocation.

The server, not the requesting agent, decides whether approval is required. A client may conservatively request approval, but it cannot mark a server-required operation as pre-approved.

## Cancellation, expiry, and retention

- The requesting client may cancel only a still-pending request it owns.
- Every request has a server-enforced expiry.
- Expired requests are never executable and produce a terminal result.
- Terminal records are retained long enough for offline requesters and audit views to receive the result.
- Payload retention should be minimized after the audit and recovery period, subject to operational and legal needs.
- Cleanup must be bounded and indexed so approvals cannot become a storage denial-of-service vector.

## Resource controls

Both nextappd and the headless client need limits for:

- pending requests per tenant, user, device, and agent;
- request payload and reason size;
- request creation rate;
- terminal-result retention;
- MCP concurrency and rate;
- local query time and response size;
- synchronization backlog size;
- cancellation and status polling rate.

No approval payload may contain arbitrary executable code or opaque operations unknown to nextappd.

## Audit and provenance

The server records approval lifecycle facts:

- who and which device requested the operation;
- agent identity;
- what exact normalized operation was stored;
- when it was created and expired;
- who and which device approved, rejected, or cancelled it;
- execution result and resulting revision.

The system does not upload model prompts, chain-of-thought, credentials, or unrelated context.

Agent provenance should become part of the durable history for mutations so clients can show who or what initiated a change and why it appeared.

## Headless container client

The container image should:

- run without Qt GUI or QML dependencies where practical;
- persist its SQLite cache and device identity on a mounted volume;
- keep NextApp credentials in an appropriate secret store or mounted secret;
- expose MCP only on explicitly configured interfaces;
- support TLS and authentication for non-local MCP transports;
- publish health and bounded operational metrics without leaking task data;
- recover synchronization, pending request status, and terminal results after restart;
- use the same agent gateway and domain operations as the desktop client.

Container deletion must not leave the associated device or agent credential permanently active without a clear revocation path in NextApp.

# Migration From Phase 1 to Phase 2

## Components to preserve

Phase 1 should deliberately produce reusable components:

- semantic MCP resource and tool schemas;
- agent identity and capability representation;
- agent gateway;
- bounded local query services;
- immutable operation description;
- approval-service interface;
- audit-event representation;
- idempotency behavior;
- use of existing client mutation APIs.

The local approval implementation is replaced by a distributed approval implementation behind the same conceptual interface.

## Components expected to change

The following phase 1 behavior is temporary:

- keeping the MCP call open;
- local-only pending request storage;
- cancellation when the UI exits;
- one UI owning the decision;
- local-only audit history;
- experimental transport and configuration UI.

No compatibility guarantee is needed for phase 1 pending requests or local audit data. The stable investment is the semantic interface and shared client-domain code.

## Recommended delivery sequence

1. Implement the phase 1 read-only MCP surface with strict limits.
2. Add one confirmed mutation, preferably `create_action`.
3. Add patch-based `update_action` and `complete_action` after the approval UI and idempotency behavior are proven.
4. Add first-class suggestions if experiments show that they are useful.
5. Use the prototype long enough to revise tool schemas based on real agents.
6. Extract reusable client-domain and agent-gateway code from GUI-specific code.
7. Add server-side pending-request persistence and distributed updates.
8. Move consequential mutation execution to the deferred server workflow.
9. Build the headless client and container image from the shared core.
10. Add broader permissions and integrations only after the audit and policy model is proven.

# Acceptance Criteria

## Phase 1

- A user can explicitly enable one local agent connection.
- The agent can read bounded synchronized NextApp state without causing arbitrary backend queries.
- Each supported mutation can be independently disabled, confirmed each time, or explicitly set to `Always allow` for the configured agent and scope.
- Mutations in `Ask` mode display a local structured confirmation before execution.
- Permitted mutations use a direct awaited call through the existing client mutation and synchronization path, without the durable queue-and-execute path.
- Rejected, expired, duplicated, explicitly cancelled, sync-aborted, and offline-aborted requests are not replayed.
- Activity is recorded locally without storing model secrets or hidden reasoning.
- Agent access can be disabled and its credential revoked.
- A full synchronization waits for MCP operations to stop and preserves the MCP idempotency and audit tables across the database swap.

## Phase 2

- A headless container client can synchronize its local cache and expose the shared MCP interface.
- A mutation requiring approval is persisted exactly once as an immutable pending request.
- Eligible active devices receive it, and newly connected devices receive it while it remains pending.
- The first valid decision wins atomically and is broadcast to other sessions.
- Approval triggers authoritative revalidation and at-most-once logical execution.
- Rejection, cancellation, expiry, validation failure, and success all produce durable correlated results.
- Server and client restarts and temporary network loss do not lose pending requests or retained results.
- No agent receives direct database access, unrestricted server queries, or integration credentials through the model.

# Open Questions To Resolve During Phase 1

- When suggestions are added, should they be synchronized immediately or remain local until accepted?
- What compact context shape works well for small local models?
- Which additional revision or precondition fields should mutation tools expose beyond the initial action version?
- How should long-running MCP requests and cancellation be represented by target agent clients?
- Which operations are safe enough to permit without confirmation later?
- How should agent identities and credentials be created, displayed, rotated, and revoked?
- Which audit details belong in normal object history versus a separate agent activity view?

These questions are reasons to build phase 1; they should not delay the local experiment unless they affect its security boundary.
