# LightHostModern documentation

LightHostModern is a Windows-only audio plugin host. A JUCE host process owns the audio stream, plugin chain, persistence, and notification-area lifetime, while a native WinUI 3 process presents the interface and exchanges commands and snapshots with the host through a local named pipe.

The application is organized into **Dashboard**, **Audio**, **Plugins**, **Settings**, **Diagnostics**, and **Support me**. Diagnostics and Support me can be hidden from Settings. The documents below explain both the public behavior of those areas and the internal workflows behind them.

## Application areas

- [Dashboard](dashboard.md) - Read backend, device, stream, meter, plugin, latency, CPU, and recovery status.
- [Audio](audio.md) - Select the audio backend and devices, then configure channels, sample rate, and buffer size.
- [Plugins](plugins.md) - Scan plugin folders, manage the installed database, and build the running serial chain.
- [Settings](settings.md) - Configure startup, tray behavior, VST2, device persistence, enabled devices, language, layout, material, and icon.
- [Diagnostics](diagnostics.md) - View local performance, reliability, stream, latency, and processing counters.
- [Support me](support.md) - Open the Ko-fi, repository, and video showcase actions.

## Internal architecture and workflows

- [Architecture and IPC](architecture-and-ipc.md) - Understand the host/WinUI process split, startup sequence, named-pipe protocol, snapshots, and version counters.
- [Audio processing](audio-processing.md) - Follow audio from the selected device through immutable chain snapshots, plugin slots, bypass compensation, meters, and failure guards.
- [Persistence and recovery](persistence-and-recovery.md) - Learn how audio choices, channels, plugin state, quarantine, safe mode, and device retry are stored and restored.
- [Tray and window behavior](tray-and-window.md) - Understand background lifetime, UI launch/focus, close-to-tray, startup registration, and responsive navigation.
- [Localization](localization.md) - Add a community translation using the runtime JSON catalogue.
- [Build and release](build-and-release.md) - Build the host and WinUI shell and generate the MSI and portable packages.

## Typical workflow

1. Start the host and open the WinUI interface.
2. Configure the stream on [Audio](audio.md).
3. Add plugin folders and scan them from [Plugins](plugins.md).
4. Add installed plugins to the running chain, reorder them, and open their editors.
5. Review [Settings](settings.md) for recovery and background behavior.
6. Use the [Dashboard](dashboard.md) to monitor the active stream and diagnose failures.

LightHostModern processes audio only while the host process is running and a usable audio device is open. Closing only the WinUI window can leave the host and chain active when close-to-tray is enabled.

- [Issue 6 and PR 5 implementation](issue-6-implementation-plan.md) — approved scope and local validation status.
- [Channel selection and main mono output](audio-channels-followup-plan.md) — approved follow-up for Individual/Pairs selection and independent output mono, with local implementation and validation status.
- [Issue 7 scanner fixes and verbose logging](issue-7-scanner-and-verbose-logs-plan.md) — approved implementation scope for discovery, retries and diagnostic captures.
- [Issue 7 validation](issue-7-validation.md) — local results, measurements and remaining compatibility/manual checks.
- [UI lifetime and sidebar validation](ui-lifetime-and-sidebar-validation.md) — forced-exit, close-to-tray and sidebar preference behavior and validation.

- [Version 1.4.1 validation](release-1.4.1-validation.md) — release tests, package inspection and remaining environment-dependent limits.
