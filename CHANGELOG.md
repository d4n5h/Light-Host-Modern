# Changelog

## [1.4.0](https://github.com/heide-oficial/Light-Host-Modern/releases/tag/v1.4.0) — 2026-09-21

### Added

- Independent Individual/Pairs selection for input and output channels, including partially selected pairs and device-specific preferences.
- Optional Mix inputs to mono before the plugin chain and independent Main output to mono monitoring for physical outputs 1 + 2, with smooth transitions and preserved auxiliary outputs.
- Whole-app CPU, resident RAM and committed memory readings, with English and Brazilian Portuguese explanations for every Diagnostics metric.
- Current dBFS readings beside the Dashboard's existing bar meters and clear notices when the selected audio device is unavailable.

### Removed

- Automatic fallback to a different device when audio recovery is Disabled.
- Unrestricted recursive cleanup of legacy installation folders; migration now backs up and verifies known app files while retaining unknown files and user settings.

### Improved

- Standardized current app names, executables, project folders and preference paths as LightHostModern, with migration and compatibility for older installations and update clients.
- Reorganized Audio into Devices, Format, side-by-side Input/Output settings, and separate Input/Output channel lists. Mono and selection controls align to the right; Settings is now the last sidebar item.
- More sensitive logarithmic meters, smooth decay and short peak retention. Fixed-width dBFS fields sit to the left of the bars without shifting them as values change.
- Versioned installer filename, LightHostModern-1.4.0-Setup.msi, alongside the identical LightHostModern-Setup.msi alias required by older updaters.
- Updated application documentation, screenshots and contributor credits for [k-ross's PR #5](https://github.com/heide-oficial/Light-Host-Modern/pull/5) and [Log1cFX's issue #6](https://github.com/heide-oficial/Light-Host-Modern/issues/6).

### Fixed

- Device creation and recovery silently substituting another backend or input/output device when the selected configuration cannot open.
- Diagnostics hover explanations being replaced during live updates, preventing tooltips from appearing reliably.
- Interrupted preference migration replaying after an intentional reset, and unsafe removal of files left by older installers.

[Full comparison with v1.3.1](https://github.com/heide-oficial/Light-Host-Modern/compare/v1.3.1...v1.4.0).

## [1.3.1](https://github.com/heide-oficial/Light-Host-Modern/releases/tag/v1.3.1) — 2026-09-13

### Added

- None.

### Removed

- None.

### Improved

- None.

### Fixed

- Corrected the overly narrow Compact layout by increasing its maximum content width from 780 to 1000 device-independent pixels, while preserving aligned headers, cards and plugin toolbars, responsive sizing, and scrollbars at the window edge.

[Full comparison with v1.3.0](https://github.com/heide-oficial/Light-Host-Modern/compare/v1.3.0...v1.3.1).

## [1.3.0](https://github.com/heide-oficial/Light-Host-Modern/releases/tag/v1.3.0) — 2026-09-12

### Added

- Global output mute and latency-compensated chain bypass in the Running toolbar and notification-area menu.
- Custom names for running instances and installed plugins, original-name restoration, and inherited names when adding plugins to the chain.
- Plugin details with original identity and bus information, instance position swapping, manufacturer grouping, and visual status badges.
- A dedicated Diagnostics page for DSP/process CPU, audio reliability, stream format, latency, and processing activity. Its General setting can hide the page and stop collection after confirmation.
- Isolated plugin scanning with progress, cancellation, readable per-path failures, and selected or complete retries.
- Versioned session files with atomic writes, recoverable backups, and migration of existing chains and plugin states.

### Removed

- Plugin discovery inside the audio host process; a separate scanner worker now handles each module.
- The old plugin-database action menu. Scanning and folder management share Scan for plugins; Remove missing and Clear database are in Settings.
- Legacy duplicate-instance identity workarounds and blocking host/UI message handling.

### Improved

- Redesigned Running and Installed views with consistent cards, search/action toolbars, grouped context menus, and connected manufacturer groups.
- More responsive dashboard peak meters, refreshed independently at up to 20 Hz while the Dashboard is visible.
- Compact layout alignment, separate input/output channel cards, checkbox and status alignment, scrolling, modal sizing, Windows materials, and repository buttons.
- Asynchronous host/UI communication, lazy page creation, and virtualized plugin lists that preserve selection, focus, and scroll position.
- Audio processing for larger channel layouts and callback blocks, smoother bypass/mute transitions, and coordinated plugin preparation and state capture.
- Device recovery that preserves unavailable preferred devices and respects enabled backend/device restrictions.
- Incremental scanning with cached metadata and preservation of completed results after cancellation or failure.
- Cancellable, streamed update downloads with progress, package verification, portable ZIP support, and orderly host shutdown before MSI updates.
- Release packaging that keeps host, UI, scanner, and update helper together and checks the exact portable payload.

### Fixed

- Initial high-DPI windows extending beyond the monitor work area.
- Stale WinUI files being reused in portable packages after interface-only changes.
- Outdated Windows executable version metadata surviving incremental builds; packaging now verifies all four app executables against the release version.
- Truncated IPC messages, late callbacks accessing expired state, and commands targeting the wrong instance after sorting or reordering.
- Duplicate instances losing source state or bypass settings, and empty saved state falling back to stale legacy values.
- Driver fallback bypassing device restrictions or erasing a saved preferred configuration after initialization errors.
- VST3 bundle identifiers being rejected, VST2 scans entering other plugin bundles, and missing scan paths failing silently.
- Plugin preparation/state capture overlapping audio callbacks, and valid plugins being skipped for large channel layouts or oversized blocks.

[Full comparison with v1.2.2](https://github.com/heide-oficial/Light-Host-Modern/compare/v1.2.2...v1.3.0).
