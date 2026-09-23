# Scanner recovery and verbose diagnostic captures

- Correct VST3 bundle/binary class comparison and retain complete class IDs without replacing persisted plugin IDs.
- Confirm catalogs through an isolated factory; validate each class separately with partial checkpoints and targeted retry.
- Renew enumeration timeout from real progress; distinguish its total limit, preserve successful roots/classes, and resolve old root failures.
- Consolidate scan roots, handle optional locations/architecture/link cycles, batch discovery results, and overlap enumeration with bounded validation.
- Add restart-armed verbose capture, process-wide stop, native TXT export, recovery after interruption, and storage limits.
- Add English/PT-BR controls and descriptions, access to pending captures independently of performance metrics, and actionable scanner details.
- Put the troubleshooting logs card first in Diagnostics with the Settings icon/text/On-Off toggle layout. Use a blue waiting-for-restart link to reopen the confirmation dialog; keep log management and status out of Settings.
- Simplify active capture to a blue stop-and-save link without the redundant Active label, timestamp or size. Hide Disabled and Logs saved text when off; preserve error feedback and pending-save actions.
- Add regression coverage for productive workers, root recovery, per-class crash/timeout, log lifecycle/export/privacy and capture limits.
- Buffer complete-content fingerprint reads, avoid repeated VST3 factory enumeration, and validate single-class VST3 modules in the existing isolated catalog process.
- Replace repeated full-catalog checkpoint writes with bounded checksummed class journals and index known-class/ID lookups. Preserve separate workers for multi-class validation.
- Add verbose stage/process timing and a summary utility. Add journal-recovery, buffered-hash and combined-validation regression cases, validated for the 1.4.1 release.

Included in version 1.4.1. Release validation is recorded in [docs/release-1.4.1-validation.md](../../docs/release-1.4.1-validation.md).

- Stop the audio host when its UI is forcibly ended, while preserving normal close-to-tray behavior. Bound shutdown when a plugin is stalled.
- Add a localized Appearance preference for a collapsed or expanded sidebar whenever the UI opens, including reopening from the tray.
