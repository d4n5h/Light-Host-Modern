# Changelog 01 — runtime and persistence

Development notes recorded while local builds still identified themselves as 1.2.2. Later UI refinements are recorded in changelog-02.md.

### Added

- CTest regressions for IPC lifetime, message transport, protocol validation, WinUI JSON/FIFO transport, and simulated device-creation policy.
- IPC 4 command and event channels with host-session identities, retained operation results, typed arguments, structured errors, and revision-bound snapshot pages. Host and UI must be updated together.
- Explicit temporary profiles with separate preferences, sessions, cache, logs, instance identity and IPC endpoints; test hosts wait for an explicit audio-device selection.
- Versioned session storage with exact legacy migration, distinct duplicate states, recoverable backups, debounced atomic writes, and visible save/recovery errors.
- Instance rename/reset, UUID position swapping, original plugin details and buses, and optional manufacturer grouping in Installed.
- Per-channel and aggregate 300 ms RMS, peak, 1.5-second peak hold, persistent clipping and explicit reset, plus separate DSP/process CPU, xrun, MIDI overflow and latency diagnostics.
- Streamed MSI/portable ZIP update downloads with cancellation, incremental SHA-256, payload validation, and a helper that waits for durable session shutdown before applying an MSI.
- Persistent UUIDs for active instances, including missing plugins, with collision repair and preservation of existing state keys.
- Global output mute and latency-compensated chain bypass in Dashboard, Running, and the tray, sharing host-owned runtime state.
- Isolated WinUI regressions for global controls, keyboard operation, and close/reopen state synchronization using a simulated IPC host.
- Simulated DSP regressions for channel layouts, large blocks, MIDI offsets, bypass, latency, and coordinated suspension.
- Dedicated `LightHostScanner.exe` with one isolated worker per module, a 60-second timeout, and Windows Job Object cleanup.
- Scan progress, cancellation, per-file failure summaries, and explicit retry in WinUI, localized in English and Brazilian Portuguese.
- Scanner regressions for crash, timeout, cancellation, response validation, multiple identities, cache reuse, and preservation of completed results.
- Scanner regression for abrupt owner termination, verifying that both the worker and its descendant are terminated.
- Opt-in Dragonfly Reverb scanner integration fixture with a pinned official download, Unicode paths, channel/identity checks and serialized cache reuse.

### Removed

- Stack-reference captures in queued IPC callbacks, blocking pipe flushes, and manual JSON substring parsing in the WinUI shell.
- Unused legacy plugin-label parsing helpers.
- Plugin discovery and potentially blocking filesystem enumeration running inside the audio host process.
- Synthetic vendor UIDs for duplicate instances and callback-side shared ownership of the processing chain.

### Improved

- WinUI requests run asynchronously through a FIFO, with complete message reads and cancellation when the interface closes.
- Backend changes apply complete device setups directly and restore only permitted previous setups.
- Device recovery uses configuration generations to invalidate obsolete retries and preserves absent configured targets without reopening blocked devices.
- Audio, Plugins, Settings and Support pages are created on first access and retained. Running/Installed use observable UUID models and virtualized lists that preserve focus, selection and scrolling.
- Visible meters update at up to 20 Hz, visible diagnostics at 1 Hz and heartbeat at five-second intervals. Minimized UI suspends visual telemetry.
- Protocol mismatch and uncertain command results have English and Brazilian Portuguese messages.
- Audio processing supports up to 256 channels and segments oversized callbacks using prepared audio views.
- Individual bypass keeps plugins processing and maintains delayed dry audio with a 5 ms transition.
- Reused plugin processors retain their open editors during chain changes.
- Running actions and drag-and-drop retain instance IDs across filtering, sorting, and asynchronous requests; obsolete IDs fail explicitly.
- Global controls use 5 ms transitions and keep plugins processing. Mute applies after dry/wet selection; both flags reset only on host startup.
- Tray labels share the English and Brazilian Portuguese JSON catalogs embedded in the host.
- Scans queue asynchronously, reuse unchanged cached descriptions, and publish validated batches without removing known plugins after interruptions.
- Scan progress copies at most 100 failure entries while retaining the complete failure count and retry list.
- Versioned scanner metadata records declared and verified buses, content fingerprints and complete revision-bound failure pages with selected/all retries.
- Local Release builds run regression checks and produce inspected MSI/ZIP packages from one verified WinUI output, excluding third-party test fixtures.

### Fixed

- VST2 discovery treating DLLs inside LV2 and VST3 bundles as standalone VST2 candidates.
- Valid Windows VST3 descriptions being rejected when JUCE identifies the binary inside the requested bundle; cache matching now preserves that original identifier.
- Missing or untraversable scan paths completing silently without an enumeration error.
- Cancelled scan enumeration and cache hits updating progress after their generation was invalidated.
- Late IPC callbacks accessing expired completion state or a destroyed server.
- Truncated named-pipe messages and incorrect byte counts for immediate overlapped partial reads.
- Driver creation bypassing device restrictions during JUCE fallback and restart attempts.
- Audio initialization/recovery errors deleting the saved audio-device preference.
- UTF-8 conversion writing a terminator beyond the allocated string.
- Plugins being skipped for expanded layouts or oversized callback blocks, and temporary channel-pointer allocation in expanded audio views.
- Plugin preparation and state capture overlapping active audio callbacks; delay storage updates now run under coordinated suspension.
- Duplicating a plugin failing to copy the source state and bypass setting, and explicit empty values falling back to stale legacy state.
- Saved state being handed to a replacement VST2 class before checking the actual plugin identity.
- Repeated generic-editor creation and stale WinUI payload staging after a UI-only build.
- Initial high-DPI windows extending beyond the monitor work area, and keyboard focus being lost while global commands were pending.

Validation evidence and remaining hardware, accessibility, performance and disposable-Windows installer checks are tracked in `docs/completion-progress.md`. This changelog does not declare the full validation matrix complete.
