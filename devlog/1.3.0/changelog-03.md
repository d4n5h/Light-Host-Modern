# Changelog 03 — release 1.3.0

## Added

- Native WinUI file/product version metadata and a packaging gate requiring all four application executables to report 1.3.0.
- Current Dashboard, Audio, Plugins, Settings and Diagnostics screenshots in the README.

## Removed

- Outdated externally hosted README screenshots, replaced with repository assets.

## Improved

- Public changelog aggregated into Added, Removed, Improved and Fixed, describing the final behavior relative to v1.2.2.
- README and screen/architecture/build documentation updated for the final scan dialog, Settings maintenance, diagnostics preference, plugin actions, session storage and update workflow.
- Release configuration explicitly disables realtime allocation-audit instrumentation.

## Fixed

- Incremental JUCE resource generation now depends on the generated metadata file, so Windows executable versions follow the project version.
- WinUI source fingerprints now include resource scripts, preventing version-resource changes from reusing a stale UI payload.

## Release verification

- Release x64 host, UI, scanner and update helper compiled successfully; all four report file and product version 1.3.0.
- All 18 CTest regressions passed. All four package inspection scenarios passed, including MSI upgrade identity/scope and stale-UI rejection.
- Both distribution archives passed updater package validation. The portable verification compared all 249 extracted files with the delivered ZIP.
- Five native UI captures were visually reviewed using an isolated snapshot host. All local links across README, CHANGELOG and 33 documentation files resolve.
- MSI metadata and payload were inspected; no installation/uninstallation was performed on the user's Windows session. Historical hardware and extended validation matrices are not represented as newly completed.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| LightHostModern-Setup.msi | 26095680 | `0b665899fd55014eae31eac37f2210e931b10eea75e306a9e18a416ae5d298ee` |
| LightHostModern-Portable.zip | 30863685 | `59bc6f9ef9e4a41bc1add8d07ecd33eef80d5831988b46a3725dc2751e4cd102` |
