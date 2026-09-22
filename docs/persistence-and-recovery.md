# Persistence and recovery

LightHostModern stores enough state to restore the audio setup, installed database, and running chain, while providing recovery paths for unavailable devices and unsafe plugins.

## Stored state

JUCE application properties retain host-owned data such as:

- selected backend, input/output device, sample rate, and buffer size;
- per-device channel masks;
- installed plugin database;
- installed-plugin custom names;
- failed-plugin quarantine;
- device persistence and blocklist configuration;
- startup, tray, VST2, and icon preferences.

Running order, instance IDs, custom instance names, bypass state, and processor state are stored in a versioned session file beside the host preferences. Atomic replacement and recoverable backups protect session writes; existing settings are migrated without merging distinct duplicate instances. See the [session contract](session-contract.md).

The WinUI shell stores interface-only options in `%LOCALAPPDATA%\LightHostModern\ui-settings.ini`, including language, layout, material, support visibility, and custom scan paths.

Settings writes are debounced during interactive operations and flushed explicitly during shutdown.

## Preferred-device recovery

Mono preferences use the existing length-delimited backend/input/output identity encoded as Base64. Input mono retains its `monoInputsV1_` key; output mono uses `monoOutputV1_` with the same identity and defaults to false. Channel masks are excluded from that identity. The host restores both settings when loading the chain, independently of whether WinUI is open.

WinUI saves `InputMode` and `OutputMode` under `AudioChannels.<preferenceKey>` in its existing `ui-settings.ini`. Missing values mean Individual inputs and Pairs outputs. The host supplies `preferenceKey` so the UI does not recreate the identity format. Changing the presentation writes only UI preferences and refreshes channel rows; it never sends an audio reconfiguration.

Device persistence supports three modes:

- **Disabled** - no preferred-device retry policy or default-device fallback. Only the exact saved device may open; its disappearance stops processing.
- **Last selected device** - remembers the last manual working configuration.
- **Custom device** - uses a backend/device choice saved from the preferred-device dialog.

The retry interval and maximum-attempt settings control the recovery loop. Failed attempts update the recovery state shown on the Dashboard. **Retry now** resets the paused state and starts an immediate attempt.

Disabled backends and devices are excluded from both manual selection and automatic recovery. If the current device becomes blocked, the host closes it and reports why.

## Audio configuration failures

Before changing a device, the engine retains the previous setup. If a new configuration fails to open, especially during ASIO switching, the previous working setup is restored when possible. The UI receives the host's last configuration error.

The audio watchdog can retry stopped or failed devices after sleep, driver restart, or Windows Audio lifecycle changes.

## Plugin state and quarantine

Running slots use persistent IDs so processor state remains associated with the correct instance across reorder and removal. Legacy state keys are imported during session migration; after migration, the versioned session is authoritative. Recovery preserves damaged originals rather than silently replacing a session with an empty chain.

Failed plugins can be marked with `plugin-failed-*` settings. This prevents repeatedly restoring a plugin known to fail during load or processing.

## Recovery command-line options

| Option | Behavior |
| --- | --- |
| `--safe-mode` | Starts without restoring the active plugin chain and disables normal preferred-device restoration for that launch. |
| `--no-restore-active-plugins` | Skips only active chain restoration. |
| `--reset-settings` | Removes the main settings file and recent crashed-plugin list before startup. |
| `--clear-failed-plugins` | Removes failed-plugin quarantine keys. |
| `--debug` | Opens a console and writes detailed host/UI diagnostics. |
| `-multi-instance=<suffix>` | Uses an isolated settings suffix for advanced testing. |

## Debug logs

With `--debug`, the host writes timestamped files under `%APPDATA%\LightHostModern\Logs`. Logs include startup, IPC, device selection, plugin loading, chain rebuilds, and fatal crash context when available. Debug logging is disabled during ordinary launches.

## Product name migration

On the first normal start, the host copies the complete `Light Host Modern.settings` family into the canonical `LightHostModern.settings` location before opening preferences. Session primary, backup, pending candidates, damaged archives and plugin quarantine are copied byte-for-byte. Staged files and a durable manifest allow interrupted publication to resume. Originals are retained. Existing canonical recovery candidates take precedence even when invalid, so migration cannot hide damaged newer data with an older session. Conflicting pending migrations stop startup and preserve both versions.

WinUI preferences already use `%LOCALAPPDATA%/LightHostModern/ui-settings.ini` and keep that path. Test profiles never import real user preferences. The WinUI package registration identity remains `LightHost.WinUI` for compatibility.
