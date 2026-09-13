# Changelog 02 - release package verification

## Added

- None.

## Removed

- None.

## Improved

- Rebuilt the host, WinUI interface, scanner and update helper for the Windows x64 MSI and portable ZIP. All four executables report version 1.3.1.

## Fixed

- None beyond the Compact layout correction recorded in changelog-01.

## Validation

- All 18 CTest cases and all four package inspection scenarios passed.
- Portable verification extracted the delivered ZIP and matched all 249 payload files by SHA-256.
- MSI inspection confirmed version 1.3.1, x64 architecture, machine scope, upgrade identity, shortcuts and legacy migration metadata. Installation was not performed on the development machine.
- The existing local README was preserved without content changes.
