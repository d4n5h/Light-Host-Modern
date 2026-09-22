# Changelog 01 — local modernization and reviewed mono-input integration

## Added

- Optional **Mix inputs to mono** setting, saved for each backend/input/output device pair. Enabled inputs are summed before plugins and bypass capture, with a 5 ms transition. Mono plugin output is centered; stereo output remains stereo. Summing can exceed 0 dBFS and is intentionally not normalized.
- Individual input-channel selection, whole-app CPU, private resident/committed memory and localized diagnostic explanations.
- Current dBFS text beside the existing 28-segment meters, a logarithmic -60..0 dBFS bar scale and short peak retention independent of Diagnostics. Numeric readings can exceed 0 dBFS. No Max field was added.
- Dashboard and Audio notices for unavailable, unconfigured or suspended audio.
- Resumable, byte-preserving preference migration and verified legacy-install cleanup with backups and an explicit payload allowlist.

## Removed

- Automatic default-device fallback when recovery is Disabled.
- Recursive removal of a legacy installation path taken directly from the registry.
- Unused EXE/IExpress release-building code; current releases use MSI and ZIP.

## Improved

- Own product names, source/project folders, namespaces, targets, binaries and current documentation use `LightHostModern`.
- Release 1.4.0 produces `LightHostModern-1.4.0-Setup.msi`, a byte-identical `LightHostModern-Setup.msi` compatibility alias and the existing portable ZIP name.
- Compatibility exceptions are deliberate: repository URL, WinUI package registration, MSI UpgradeCode and transitional MSI ProductName stay compatible. Small launchers preserve the four filenames required by the 1.3.1 updater. Windows' installed-program entry retains the old spaced MSI product name during this transition.
- Memory collection runs outside audio callbacks, at about 1 Hz while Diagnostics is visible; the request lease expires after two seconds. Plugins are included in host consumption. Shared pages are excluded from memory totals.
- Existing retry limits, Plugin database behavior and auxiliary-channel routing remain supported.

## Fixed

- Strict device creation now protects the selected backend/input/output against JUCE's internal fallback paths.
- The contribution in PR #5 was adapted locally with click-resistant switching, correct mono centering, device-specific persistence and generation-checked IPC. The original PR was not merged or modified.
- Mono synchronization ignores programmatic changes already confirmed by the host, preventing their resubmission as new user commands.
- Named Audio labels are localized before the lazy page enters the visual tree. Completed preference migration is not replayed after an intentional reset; legacy-file backups are flushed before their originals can be removed.
- MSI major upgrade removal occurs inside the rollback boundary; legacy EXE cleanup runs only after commit and retains unknown files and recovery copies.

## Validation context

- Release host and native WinUI builds succeeded. CTest: 20/20 passed, including migration, device recovery, realtime mono/bypass transitions, callback allocation checks, meters, sessions, scanners and updater validation.
- Isolated profile integration passed without opening audio or changing production preferences/startup registration.
- Package inspection passed: metadata/hashes, required payload, upgrade identity, safe migration action, identical MSI aliases and stale WinUI staging rejection.
- The actual local 1.3.1 update helper accepted the new MSI alias in **validate** mode. No installation was performed by this check.
- Real-host UI smoke passed in English/PT-BR and Compact/Expanded at 192 DPI. All 21 diagnostic values exposed translated help text; live CPU and committed memory were present. Screenshots were captured and inspected.
- The UI smoke script also supports simulated channels/signals and an optional short resource comparison; detailed evidence and final execution results are recorded in `docs/issue-6-implementation-plan.md`.
- Simulated UI matrix passed 16 mono toggles, four individual input selectors, the paired output selector and -12.0/-6.0 dBFS readings. A short real-host resource comparison was recorded, with warm-up/order limitations stated explicitly.

No remote write, commit, push, PR change, release publication or repository rename was performed. Local packages are unsigned. Physical hot-plug, audio-driver listening tests, 96/144 DPI and real EXE/MSI lifecycle testing in a disposable Windows environment remain outstanding; package inspection does not establish those results.
