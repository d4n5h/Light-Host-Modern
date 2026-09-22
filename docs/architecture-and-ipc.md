# Architecture and IPC

LightHostModern uses two cooperating native desktop processes. This keeps the realtime host independent from the lifetime and rendering work of the WinUI shell.

## Process model

### Host process

`LightHostModern.exe` owns:

- JUCE application lifetime;
- audio device and callback;
- plugin formats, database, instances, editors, and saved state;
- realtime serial processing;
- settings and recovery state;
- notification-area icon and native menu;
- named-pipe server;
- debug and crash diagnostics.

The main modules are:

- `Source/HostStartup.cpp` - command-line parsing, application properties, recovery options, and JUCE startup.
- `Source/IconMenu.cpp` - tray icon, UI launch/focus, and quit coordination.
- `Source/AudioEngine.*` - device management, plugin database, running chain, settings, and snapshots.
- `Source/RealtimeHostProcessor.*` - realtime processing and immutable chain snapshots.
- `Source/HostIpcServer.*` - named-pipe requests, commands, state snapshots, and telemetry.
- `Source/PluginWindow.*` - plugin editor windows and their saved positions.

### WinUI shell

`LightHostModernWinUI.exe` owns the Windows 11-style interface, navigation, dialogs, localization, update check, UI preferences, and IPC client. It does not process audio or own plugin instances.

## Startup sequence

1. The host parses command-line options and enables diagnostics when requested.
2. JUCE application properties are opened, reset, or repaired as requested.
3. `AudioEngine` initializes formats, audio state, plugin database, and optionally the saved chain.
4. `HostIpcServer` creates `\\.\pipe\LightHostModern-<host-pid>`.
5. The notification-area icon is created.
6. Opening the interface launches `LightHostModernWinUI.exe --host-pipe="<pipe>"`.
7. The shell requests a complete snapshot and begins periodic telemetry/state checks.

If the shell window already exists, the host restores and focuses it rather than opening a duplicate UI.

## Named-pipe protocol

The transport is a local Windows message-mode named pipe. Requests and responses are UTF-8 JSON, limited to 4 MiB per message. Both ends use cancellable overlapped I/O and assemble partial reads. The shell queues requests in FIFO order outside the UI thread; the server dispatches commands serially on the JUCE message thread. Queued callbacks own their completion state and cannot access a destroyed server.

Each request has `version`, `id`, `command`, and a typed `args` array. Mutations also carry the `hostSession` obtained from `hello` or a snapshot. For example:

```json
{"version":4,"id":"ui-42","hostSession":"session-from-snapshot","command":"set-input-channel","args":[31,true]}
```

Responses echo `version` and `id`. Errors include `error.code` and `error.message`, with `status: "error"`. The command schema lives in `Source/IpcSchema.h`. The host uses JUCE JSON and the shell uses Windows.Data.Json; names and paths are not parsed using delimiter searches. Legacy text requests and incompatible protocol versions are rejected. Rebuild and restart both processes together.

The client acknowledges a complete response before the server disconnects. Connect/read/write waits can be cancelled during shutdown; the host never calls blocking pipe `FlushFileBuffers`. Mutations return an `operationId` and `operationState` immediately, then execute serially on the JUCE controller. `operation-status` takes `[operationId, originalHostSession]` and returns queued, running, completed, failed or cancelled. Terminal responses include the original result or structured error.

Completed records are retained for at most ten minutes, bounded by 256 records and 32 MiB. In-flight operations are not expired; their admission is also bounded. Repeating the same ID and command content retrieves the existing operation; a different command using that ID is rejected. Client operation waits have an overall 120-second deadline with bounded individual exchanges. After timeout/reconnection the client queries the original ID without replaying the mutation. `host_restarted` distinguishes an old host session from a temporary disconnection; `operation_unknown` never authorizes automatic replay. Late errors are surfaced after reconciliation and the current snapshot refreshes visible state.

Shutdown rejects new operations and cancels queued work. Quit waits for a response already in transit to finish. The host does not attempt to interrupt plugin code already running on its message thread.

Read operations include:

- `state-snapshot` - complete audio, plugin, preference, and version state;
- `telemetry` - rapidly changing meters and performance values;
- `enabled-audio-choices` - backend/device data for the management dialog.

Mutation commands cover audio selection, channel masks, persistence options, plugin scanning, database actions, chain actions, startup, tray behavior, VST2, and icon changes.

Protocol 4 uses persistent `instanceId` strings for remove, duplicate, bypass, editor, move-up/down, move-to, and swap commands. Move-to and swap receive two instance IDs, resolved against the current processing order on the host message thread. An absent instance returns `instance_not_found`; it cannot redirect an old action to the new occupant of a list position. Installed actions use a stable `knownId`, derived from the original format, module identifier and class ID. Versions 1–3 are rejected before dispatch.

An ordered instance collection owns UUIDs independently of plugin descriptions. The transitional `pluginInstancesV1` adapter preserves legacy XML and exact state keys and backs up the original preferences before migration. Duplicating never modifies vendor UIDs. Reordering, filtering, missing modules, and UI recreation do not generate new IDs. Ambiguous legacy data is retained without guessing another plugin's identity. The [versioned session writer](session-contract.md) now persists this model with atomic replacement and recoverable backups.

`set-global-mute` and `set-global-bypass` accept one JSON boolean. Snapshot and telemetry responses expose `globalMuted` and `globalBypassed`. These flags belong to the host, start false, are excluded from persisted session/preferences, and increment the chain counter when changed. The Running toolbar and tray operate on that same state. The host continuously captures a dry path delayed by the chain latency, mixes it with the processed signal over 5 ms, then applies a separate 5 ms mute gain. Neither mode skips plugin processing. Delay storage is prepared under controller suspension, including after dynamic latency changes.

## Isolated discovery

`set-mono-inputs` and `set-mono-output` take one object with boolean `enabled` and positive `expectedGeneration`. The host rejects stale generations with `stale_configuration` and malformed values or an unconfigured device with `invalid_arguments`, using the normal operation error envelope. Snapshot and telemetry expose independent `monoInputs` and `monoOutput` flags. `audioSelection.preferenceKey` identifies the device combination; `audioSelection.mainOutputPairActive` indicates that processing is available and physical outputs 1/2 are both active. Enabling output mono does not reopen the stream or rebuild the chain.

Channel presentation remains local to WinUI. A pair click uses the existing generation-checked audio-selection transaction with both mask bits changed together. Presentation changes during pending operations are saved and applied after reconciliation. Mono operations use the captured generation and the existing operation/snapshot path; an old command cannot apply to a newly selected device.

Scan commands accept controller operations, whose results report queue acceptance. `begin-plugin-scan` resets an idle scan's progress; `scan-default-plugins` and `scan-plugin-path` enqueue work. `plugin-scan-status` includes scan ID, revision, activity, cancellation, module/enum/cache counts and a bounded summary of failure IDs. `cancel-plugin-scan` cancels queued/running work; `retry-plugin-scan` explicitly retries all failures while idle.

The sibling `LightHostModernScanner.exe` has separate enumeration and module examination modes, with only one child active. All filesystem operations on configured plugin locations, including fingerprints, run in that child. A kill-on-close Windows Job Object owns it before it resumes. Each enumeration root and module examination has a 60-second deadline. Immutable, bounded candidate batches preserve partial progress; a blocked root does not prevent later roots from being attempted. Canonical paths deduplicate overlapping directories, but the path sent to JUCE and the identity returned by JUCE are preserved. Junctions are not traversed recursively.

Private temporary XML uses scanner protocol 2 and correlates format, module path, ID, batch sequence and content fingerprint. VST3 static metadata accelerates enumeration through JUCE; the worker also creates each class and verifies its identity and buses. Cache version 2 records declared versus verified metadata, input/output bus names, main/auxiliary roles, enabled state and default layouts. Fingerprints include relevant file names, sizes, modification times and streamed SHA-256 digests for binaries and JSON metadata. Valid sibling classes survive another class's failure. Existing known entries survive failure, cancellation and retry; clearing the database invalidates older scan generations.

`plugin-scan-failures` takes one object with `scanId`, `revision`, `offset` and `limit` (1–100). Pages return stable failure IDs, full paths, reason, format and attempt; a changed revision returns `stale_revision`. `retry-plugin-scan-selection` takes `scanId`, `revision` and selected `ids`, preserving unrelated failures and valid results. The UI's failure dialog provides all pages and preserves selections by ID. `known-plugin-details` accepts a stable class ID and distinguishes availability verified at the last scan from unavailable metadata. Active processing remains inside the audio host and is not protected by scan isolation.

## Snapshots and version counters

The host exposes separate version counters for:

- running chain changes;
- plugin database changes;
- audio configuration changes.

The shell polls lightweight telemetry and requests the larger state snapshot only after a counter changes. This reduces serialization and UI work while preserving live meters and status.

## Failure boundaries

An empty response indicates that the pipe closed or the host stopped responding during an operation. Plugin-load commands return explicit error JSON when the host can reject a plugin safely. The shell displays the failure without pretending the plugin was added.

The process split protects the long-running audio host from routine UI recreation, but a plugin fault inside the host can still terminate audio processing. Safe mode and quarantine mitigate repeated startup failures.
