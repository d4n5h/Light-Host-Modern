# Changelog 03 — final layout and release preparation

## Added

- Separate Input/Output settings cards for mono and Individual/Pairs controls, above side-by-side Input/Output channel cards.
- Updated README screenshots captured from the actual 1.4.0 UI with isolated demonstration data, and linked credits for the issue #6 and PR #5 contributions.

## Removed

- Redundant channel-selection headings inside the channel-list cards.

## Improved

- Format follows Devices; mono and grouping controls align to the right in Settings-style rows. Channel titles, Check all / Uncheck all and lists remain together.
- Dashboard dBFS readings precede the bar meters inside a fixed 112 × 26 field. Sidebar order is Dashboard, Audio, Plugins, Diagnostics, Support me, Settings.
- Aggregated release notes follow the existing Added / Removed / Improved / Fixed format with comparison and full-changelog links.

## Fixed

- Live Diagnostics updates no longer replace the ToolTip objects every refresh; only changed explanation text is updated.
- About and the update-download user agent now report 1.4.0, matching CMake, executable resources and package manifests.

## Delivery scope

The user authorized commits, the v1.4.0 tag and a public GitHub release with portable and installer assets. Earlier local-only restrictions apply to their recorded development checkpoints, not this release preparation. Issue #6 and PR #5 must remain open; attribution links do not use automatic closing keywords.

Release host/UI builds and screenshot capture are performed for this delivery. The automated test suite is not rerun, preserving the user's instruction from the final UI iterations; earlier test results remain historical evidence in changelog-01/02. Physical hardware, other DPI scales and disposable-machine installer lifecycle coverage retain their documented limitations.
