# Changelog 02 — interface and release preparation

## Added

- Dedicated Diagnostics page and a General preference that confirms disabling it and stops diagnostics collection.
- Installed-plugin aliases inherited by new running instances, with original-name restoration.
- Status badges, manufacturer cards with connected child cards, and grouped context-menu actions.

## Removed

- Interim Database sidebar and tabbed database modal; scanning now uses one Scan for plugins dialog.
- Interim dashboard mute/bypass, textual RMS/hold controls, and per-card editor/add shortcuts.

## Improved

- Running and Installed toolbars share search widths; grouping is the first sort-menu option.
- Scan folders and the add-path editor share one scroll area; progress/results use a separate small dialog.
- Missing-entry removal and database clearing use standard Settings cards.
- Compact page alignment, native materials, modal sizing, checkbox spacing, and About repository links.
- Open editor precedes Plugin details in the Running context menu.
- Version fields and distribution metadata advance to 1.3.0; release builds disable allocation-audit instrumentation.

## Fixed

- Settings and hidden Support page crashes caused by accessing pages before their lazy initialization.
- Oversized Preferred device rows, incorrect hover surfaces, status alignment, and excessive add-path card height.
- Delayed dashboard meters by using a separate lightweight peak channel at up to 20 Hz.
- Stale portable UI payloads by staging and extracting the delivered archive with matching file hashes.
- Stale native executable version resources after incremental builds. JUCE resource generation now depends on its metadata; WinUI includes native version metadata, and packaging rejects mismatched executable versions.
