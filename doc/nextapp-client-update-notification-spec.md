# NextApp Client Update Notification

Feature specification Status: Draft Scope: NextApp client and nextappd
server configuration Protocol: Uses the protobuf changes already present
in nextapp.proto

# 1. Overall goals

-   Notify a user unobtrusively when a newer NextApp client release is
    available for the platform and distribution the client is currently
    running.

-   Use the existing authenticated NextApp client/server connection. The
    feature must not introduce a separate update service, web endpoint,
    TLS dependency, or periodic Internet request.

-   Make update availability a per-nextappd deployment policy.
    Administrators must be able to roll out releases gradually by
    region, hold back a release in one region, or advertise a
    region-specific hotfix.

-   Keep the client responsive. Update checking must add no blocking
    work to application startup or the UI thread.

-   Preserve privacy. The feature must not require additional client
    tracking, external requests, or disclosure of information beyond
    what is needed by the existing NextApp connection.

-   Keep the mechanism small. This feature is update notification and
    policy distribution, not an automatic software updater.

# 2. Existing protocol contract

The protobuf changes already present in nextapp.proto are the wire
contract for this feature. HelloReq carries only the client's
DevicePlatform. ClientUpdate is delivered separately as the clientUpdate
member of Update.what. No additional protobuf changes are required by
this specification.

## 2.1 Platform identification

HelloReq.deviceInfo.devicePlatform identifies the client
platform/distribution. The currently defined values are
UNKNOWN_PLATFORM, LINUX_NATIVE_X64, LINUX_FLATPAK_X64, MACOS_X64,
MACOS_ARM64, WINDOWS_X64, and ANDROID. The value is used only to select
the per-platform server policy.

## 2.2 Update information

ClientUpdate carries version_code, version, and required. It is
transmitted as Update.clientUpdate (field 31 in the Update oneof). It is
not part of the Hello response.

The server uses `HelloReq.deviceInfo.devicePlatform` and
`HelloReq.deviceInfo.version_code` to evaluate the active policy for the
individual session. It sends `ClientUpdate` only when that client should
be notified, and decides whether the update is advisory or required. The
client still compares the received `ClientUpdate.version_code` with its
own compiled-in version code as a defensive consistency check.
Human-readable version strings are never used for ordering.

## 2.3 Semantics

-   version_code is the authoritative monotonically increasing value
    used for comparison. It is computed from the `NEXTAPP_VERSION` string
    in the root cmake file (available already in the server and client).
    This may change in the future.

-   version is presentation text for the UI and logs. It must not be
    used to decide ordering.

-   required indicates that the server policy considers the advertised
    release mandatory for clients whose local version_code is lower. It
    is notification/policy metadata only. It does not by itself
    authorize nextappd to terminate the session or the client to
    download or execute software.

-   A ClientUpdate whose version_code is less than or equal to the
    running client's version code does not represent an available update
    and must not produce an update notification.

-   UNKNOWN_PLATFORM must not receive a guessed platform policy. The
    server should omit ClientUpdate for it. It could be used by
    experimental clients, 3rd party clients and bots/agents. 

# 3. Server configuration

Each nextappd instance can optionally have local YAML configuration describing
the release currently advertised for each supported DevicePlatform. The
configuration is centrally managed operationally (for example through
Ansible), but its scope is the individual nextappd deployment.

The command-line argument for the yaml file is `--client-versions-manifest <path>`

This deliberately separates global software publication from server
policy. A release may exist without every region advertising it.

If the server does not have a version configuration file, this functionality is
disabled and a warning written to the servers log at startup. 

## 3.1 Suggested YAML structure

``` text
client_updates:
  default:
    version_code: 2501
    version: "0.25.1"
    required: false

  linux_native_x64:
    version_code: 2501
    version: "0.25.1"
    required: false

  linux_flatpak_x64:
    version_code: 2501
    version: "0.25.1"
    required: false

  macos_x64:
    version_code: 2501
    version: "0.25.1"
    required: false

  macos_arm64:
    version_code: 2501
    version: "0.25.1"
    required: false

  windows_x64:
    version_code: 2501
    version: "0.25.1"
    required: false

  android:
    version_code: 2500
    version: "0.25.0"
    required: false
```

## 3.2 Configuration requirements

-   Entries are optional. If the exact DevicePlatform has no configured
    entry, nextappd does not advertise ClientUpdate for that platform.
    No fallback from one architecture/distribution to another is
    allowed.

-   version_code must be greater than zero and within the protobuf
    uint32 range.

-   version must be non-empty, bounded to a small implementation-defined
    length, and treated as untrusted display data by the client.

-   The YAML platform names must map explicitly to DevicePlatform enum
    values. Unknown configuration keys must produce a clear
    configuration warning or error rather than silently mapping to
    another platform.

-   required defaults to false if omitted, unless the configuration
    parser intentionally requires it to be explicit.

-   Configuration validation must happen before new policy becomes
    active. Invalid update configuration must never crash nextappd or
    prevent otherwise valid client sessions.

-   The active configuration should be represented internally as a small
    immutable or read-mostly lookup table keyed by DevicePlatform.
    
-   The `default` values can be used to avoid repeating the same values
    for all the platforms. When used, the server will use the default for
    any unspecified platform. 

## 3.3 Reloading

Prefer hot reload of the client update policy if nextappd already has,
or gains, a safe configuration reload mechanism. A reload must validate
the complete replacement policy first and then atomically replace the
active lookup table. Existing sessions must never observe a partially
updated policy.

Hot reload is desirable operationally but is not a prerequisite for the
first implementation if nextappd currently requires restart/redeploy for
configuration changes.

One approach can be to trigger an hourly timer and check if the platform
file is newer than the currently loaded version. 

## 3.4 Policy evaluation

The YAML describes the releases and constraints for a platform;
`DeviceInfo` supplies the facts about the connecting client. `nextappd`
combines the two to make a per-session decision.

At minimum, policy evaluation must support:

-   **No update:** the client's `version_code` is current for this
    server's policy. No `ClientUpdate` is sent.
-   **Advisory update:** a newer configured release is available, but
    the running version remains acceptable. Send `ClientUpdate` with
    `required = false`.
-   **Required update:** the running `version_code` falls below the
    server's accepted policy, or is otherwise covered by a configured
    mandatory-upgrade rule. Send `ClientUpdate` with `required = true`.

This decision belongs entirely to `nextappd`. Different regions may
therefore make different decisions for the same platform and client
version without changing the client or protobuf.

# 4. Server behavior

## 4.1 Hello processing

1.  Read HelloReq.deviceInfo.devicePlatform. If absent, protobuf
    optional presence is false; if present as UNKNOWN_PLATFORM, treat it
    equivalently for this feature. In either case no platform policy is
    advertised. Keep the device information in the user's session information 
    in the servers memory. 

2.  Look up the active ClientUpdate policy for the exact DevicePlatform
    value.

3.  Do not infer architecture or distribution from other client strings
    when devicePlatform is present.

4.  If a policy exists, make it available to the client through the
    existing update delivery mechanism after the update stream is 
    established. 

5.  Failure to select or deliver update information must not fail Hello,
    authentication, synchronization, or the client session.

## 4.2 Delivery through the update stream

ClientUpdate is session-local server policy, not user data. nextappd
shall synthesize it for the connection after the normal update stream
has been established. It shall not be written to the retained update log
or replayed from UpdatesReq.fromMessageId.

For ClientUpdate, Update.messageId shall be 0. Published/replayable
updates use message IDs starting at 1, so 0 explicitly identifies this
update as non-retained and outside the incremental playback sequence.
Update.op has no semantic meaning for ClientUpdate and clients must
ignore it.

The server should send at most one ClientUpdate for the active policy
when a session subscribes to updates. If the active policy is
hot-reloaded while the session remains connected, nextappd may send a
replacement ClientUpdate with messageId 0. The client must treat
replacement messages idempotently.

ClientUpdate is an Update payload. The server should deliver the
currently applicable policy promptly after the client has established
the normal update subscription. This keeps update notification inside
the existing server-to-client event path.

The implementation must avoid turning the release policy into persistent
per-user application data unless there is an existing architectural
reason to do so. It is server policy that can be recomputed from
DevicePlatform and current configuration.

If the active policy changes while a client is connected, nextappd
should publish the new ClientUpdate to affected active sessions when
practical. Otherwise clients will receive the new policy on their next
connection. Hot notification of connected sessions is recommended
because it makes regional hotpatch policy effective without requiring
reconnection.

# 5. Client behavior

## 5.1 Local version identity

Every build must expose a compile-time/current version_code and
human-readable application version. version_code is authoritative for
update comparison and must increase for every release that may supersede
an earlier release on the same distribution channel.

Android's application versionCode should be aligned with this value
where practical. Other platforms should use the same release-number
concept even if their packaging system names it differently.

## 5.2 Processing ClientUpdate

1.  Validate version_code and bound the version string before using it.

2.  If advertised version_code \<= local version_code, treat the client
    as current and clear any stale in-memory 'update available' state
    for that server policy.

3.  If advertised version_code \> local version_code, expose an
    update-available state to the UI.

4.  If required is false, the update is advisory.

5.  If required is true and advertised version_code \> local
    version_code, expose a required-update state. Required must not be
    interpreted as an instruction to download or execute anything
    automatically.

6.  Repeated identical ClientUpdate messages must be idempotent and must
    not repeatedly notify the user.

## 5.3 Persistence and dismissal

For advisory updates, the client should remember that the user dismissed
a particular version_code for the current application run so the same
release does not nag on every reconnect. This state is in-memory only:
after an application restart, the available update may be shown again. A
newer version_code may notify again during the same run.

A required update must remain visibly indicated while the client is
older than the required release. The UI may allow the immediate
notification to be closed, but it should not permanently suppress the
required-update state.

# 6. User interface

Update notification must be non-modal for normal releases. It must not
interrupt startup, synchronization, editing, timers, or other user work.

We can use the existing Notification framework in the UI, but keep the 
event local and update / reset it when the app starts.

## 6.1 Advisory update

Suggested presentation: "NextApp 0.25.1 is available." with an action
appropriate to the current distribution and a dismiss action.

## 6.2 Required update

Suggested presentation: "A required NextApp update is available:
0.25.1." The state should be more prominent than an advisory update, but
the protocol flag alone must not cause the application to terminate,
self-modify, or execute downloaded code.

## 6.3 Update action by platform

-   Android: open the appropriate application-store/update workflow.

-   Other platforms: Point to the release page on GitHub: `https://github.com/jgaa/next-app/releases`

# 7. Security and privacy

-   No additional network endpoint is introduced. Update information
    travels over the existing authenticated NextApp server connection.

-   The client must treat ClientUpdate fields as untrusted network input
    despite the authenticated connection. Strings must be bounded and
    safely escaped by QML/Qt UI components.

-   The update mechanism must never download, execute, install, or
    replace software merely because nextappd advertised a ClientUpdate.

-   The server must not accept peer-controlled values as configuration
    keys or use them to cause unbounded allocation or work.

-   DevicePlatform is only a selector for a small bounded lookup table.

-   No additional device identifier, telemetry identifier,
    current-version query, or external update-check request is required
    by this feature.

-   A compromised nextappd can falsely claim that an update is available
    or required, but it must not thereby gain code-execution capability
    through this feature. Installation remains under the platform's
    normal trusted mechanism and user control.

# 8. Performance and responsiveness

-   Platform-policy lookup must be O(1) or equivalent over the very
    small fixed platform set.

-   No filesystem access, YAML parsing, DNS lookup, HTTP request, or
    other blocking operation may occur on the UI thread as a consequence
    of receiving ClientUpdate.

-   The client-side comparison is a simple integer comparison and should
    be handled as part of normal asynchronous update processing.

-   Server configuration should be parsed at startup or reload time,
    never per Hello request.

-   Repeated delivery must be cheap and idempotent.

# 9. Operational rollout

The authoritative operational value is the YAML policy deployed to each
nextappd instance. Central automation may use shared defaults plus
per-region/per-instance overrides.

``` text
Example rollout:

1. Publish NextApp 0.25.1 through the normal platform channels.
2. Verify that the release is actually obtainable for each platform.
3. Advertise 0.25.1 on one nextappd region.
4. Observe normal operation.
5. Expand the YAML change to additional regions.
6. If a region-specific problem occurs, hold, replace, or mark a suitable release
   required only in that region.
```

Configuration rollout must not advertise a release before that release
is obtainable through the corresponding platform's supported update
channel.

# 10. Logging and observability

-   nextappd should log configuration validation failures and successful
    policy reloads at appropriate levels.

-   Debug-level logging may record that a ClientUpdate policy was
    selected for a platform, but routine logs should avoid noisy
    per-session messages unless useful for diagnostics.

-   The client may log receipt of update policy and the local comparison
    result.

-   No new telemetry is required. In particular, the feature should not
    add reporting that a specific user dismissed or installed an update.

# 11. Failure handling

-   Missing platform policy: continue normally without an update
    notification.

-   Unknown/missing DevicePlatform: continue normally without an update
    notification.

-   Malformed server YAML: reject the malformed policy or reload while
    preserving the last known-good active policy; do not expose partial
    state.
    Create a log-event on error level.

-   Malformed ClientUpdate received by the client: ignore it safely and
    log as appropriate.

-   Update UI failure: must not affect synchronization or server
    connectivity.

-   Stale server policy advertising an older version than the client:
    client ignores it.

# 12. Non-goals

-   Automatic downloading or installation of NextApp.

-   Replacing Flatpak, application-store, Windows installer, macOS
    packaging, or Linux package-manager update mechanisms.

-   A public release-discovery web service.

-   Release notes distribution through the protobuf.

-   Arbitrary download URLs supplied by nextappd.

-   Tracking update installation or user dismissal centrally.

-   Solving general protocol-version compatibility. Existing
    protocol-version negotiation remains separate.

-   Maintaining a complete historical release catalog in nextappd.

# 13. Acceptance criteria

-   A client identifies its exact supported DevicePlatform in HelloReq.

-   Each nextappd can independently configure the advertised release for
    every supported platform.

-   A server can advertise different versions from another region
    without code or protocol changes.

-   The client shows no notification when advertised version_code is
    less than or equal to its own.

-   The client shows a non-modal notification when a greater advisory
    version_code is advertised.

-   The client exposes a persistent/prominent required-update state when
    a greater version_code is advertised with required=true.

-   Repeated identical update messages do not repeatedly nag the user.

-   An invalid or absent update policy cannot prevent login,
    synchronization, or normal application use.

-   No external update-check endpoint is contacted.

-   No update is downloaded or executed automatically.

-   Changing the per-server policy requires no client/protobuf change.

-   ClientUpdate is sent as a non-retained Update with messageId = 0 and
    never participates in update replay.

# 14. Implementation notes

Keep the server-side feature behind a small policy component rather than
spreading YAML access throughout session code. A suitable conceptual
interface is:

``` text
std::optional<nextapp::pb::ClientUpdate>
clientUpdateFor(const nextapp::pb::DeviceInfo& device) const;
```

The client should likewise centralize update state in a small C++
model/service exposed to QML. QML should render state and invoke
platform-appropriate actions; it should not perform version comparison
or network processing itself.

The server may use `yaml-cpp`, and handle it with cmake (dowload, static library).
