# Release validation and version alignment

- Align native, WinUI, manifests, Windows version resources, release packaging and update requests with version 1.4.1.
- Run the complete native suite and host/audio/UI integration checks; add actual-process lifetime and end-to-end verbose-log UI regressions.
- Fix the empty-file journal test fixture and remove timing assumptions about the ordering of session-save versus chain-change events.
- Update UI automation for the current dialogs, sidebar order, logarithmic meters, virtualized lists and native toggle properties. Isolate each UI suite and launch the same self-contained shell used by the host.
- Make the native test build directory configurable and remove dependency on a local command wrapper.
- Generate and inspect portable/MSI packages; record results and compatibility/environment limits in [release validation](../../docs/release-1.4.1-validation.md).
- Preserve README.md and leave issue 7 for the maintainer to respond to and close manually.
