# Changelog 02 — channel pairs and main output mono

## Added

- Independent Individual/Pairs selection for inputs and outputs, saved per backend/input/output identity in UI preferences. Defaults remain Individual inputs and Pairs outputs. Odd final channels remain individual; partial pairs display the indeterminate state and enable both channels when clicked.
- Optional **Main output to mono**, saved by the host per device combination. It averages physical outputs 1/2 after plugins, global bypass and mute, with a 5 ms transition. Lone principal outputs and auxiliary outputs remain unchanged.
- English/PT-BR labels, tooltips, accessible names and an explanation when the main output pair is incomplete.

## Removed

- None.

## Improved

- Both mono controls head their respective channel cards, above Input channels / Output channels.
- Switching presentation preserves channel masks and never reopens the audio device. Pair clicks update both bits in one existing generation-checked transaction.
- Presentation choices made during pending operations are saved immediately and reconciled afterward. Host-provided device identity avoids duplicating key construction in WinUI.
- Physical output mapping and counts are prepared outside the callback. Mono adds no callback allocation or lock, and meter calculation avoids scanning unused packed input channels as outputs.

## Fixed

- Both mono commands now preserve structured `stale_configuration` / `invalid_arguments` errors through the IPC operation envelope, instead of reducing them to `command_failed`.
- Missing or repeated channel names use localized fallback labels and stable physical indices.
- The simulated host preserves the wire request identity during audio-selection handling; UI tests fail on fixture exceptions rather than accepting a state mutation with a failed reply.

## Validation

- Release host and WinUI builds succeeded. CTest passed 20/20, including new output routing cases at 48/96 kHz, sparse physical masks, auxiliary channels, equal/opposite signals, plugin mono/stereo layouts, both mono controls, global bypass, mute, output metering with Diagnostics disabled and the existing callback allocation audit.
- Real-host isolated-profile integration passed, including rejection of stale output-mono commands without opening audio or modifying production preferences/startup registration.
- Final UI and package evidence is recorded in [the follow-up plan](../../docs/audio-channels-followup-plan.md).
- Final packaged UI passed English/PT-BR × Compact/Expanded at 192 DPI, including both mono controls, partial pair clicks, grouping persistence across UI restarts, last-choice retention during a delayed mono command, and 0/1/2/3/8 channel layouts with missing/repeated names. Screenshots were inspected and the fixture error log remained empty.
- MSI/ZIP payload and metadata inspection passed. The actual 1.3.1 helper accepted the regenerated MSI in validate-only mode; no installation occurred. Packaged WinUI matches the current build by SHA-256.

No commit, push, remote issue/PR change, publication or installation is part of this work. Physical device switching/hot-plug and listening checks, 96/144 DPI, and installer lifecycle tests in a disposable Windows environment remain external validation tasks.
