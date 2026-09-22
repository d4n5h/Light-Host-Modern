# Build and release

## Isolated global-control UI check

After building WinUI Release, run `Tests/GlobalControlFakeHost.ps1` in a separate hidden PowerShell process and launch the UI with `winapp run WinUI/x64/Release/LightHostModern.WinUI --manifest WinUI/LightHostModern.WinUI/Package.appxmanifest --exe LightHostModernWinUI.exe --detach --json -- --host-pipe=\\.\pipe\LightHostModern-global-ui-test`. Pass the returned process ID to `WinUI/ui-tests-global-controls.ps1 -AppPid <id>` from the repository root. Use only that simulated connection for this script, never a live audio session. It checks both pages, keyboard activation, close/reopen synchronization, and closes the test UI. Write the fixture-owned file `out/global-ui-test/stop` to stop the helper. Logs and the Dashboard screenshot stay under `out/global-ui-test`.

LightHostModern has two native build systems: CMake builds the JUCE host, while MSBuild builds and packages the C++/WinRT WinUI shell. The release script combines both outputs. Version 1.4.0 uses the same version in CMake, the WinUI manifests, About, the updater, and the MSI/portable metadata.

## Requirements

- Windows 10 version 1809 or newer; Windows 11 is recommended.
- Visual Studio 2022 or newer with **Desktop development with C++**.
- MSVC v143 x64 toolchain and MSBuild, including UWP C++ x64 and base XAML build tools. Desktop C++ alone is insufficient. The WinUI project targets Windows SDK 10.0.26100.0.
- Windows SDK `10.0.22621.0` or newer; the WinUI project uses a newer installed SDK when configured.
- CMake 3.22 or newer.
- Git and network access for CMake dependency retrieval.
- Windows App SDK / WinUI 3 tooling available through Visual Studio and NuGet restore.

Default fetched dependencies are JUCE 8.0.13, Steinberg VST3 SDK `v3.8.0_build_66`, the ASIO SDK, and Xaymar VST2 headers when that provider is enabled.

## Build the host

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Windows.ps1"
```

Useful variants:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Windows.ps1" -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Windows.ps1" -EnableVst2 ON -Vst2Provider XAYMAR
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Windows.ps1" -EnableVst2 OFF
```

The default Release host output is:

```text
out\build\windows-vs2022\LightHostModern_artefacts\Release\LightHostModern.exe
```

## Build the WinUI shell

CTest regression executables are enabled by default (`BUILD_TESTING=ON`). After building Release:

```powershell
ctest --test-dir out/build/windows-vs2022 -C Release --output-on-failure
```

The tests use temporary local pipes, Windows JSON, and simulated device types. They do not launch the audio host or alter the user's audio settings. They cover IPC lifetime, partial messages, typed/versioned requests, WinUI asynchronous FIFO transport, and guards on driver creation. These tests do not replace plugin DSP, recovery, UI accessibility, scan-isolation, persistence, or updater validation.

Build `WinUI\LightHostModern.WinUI\LightHostModern.WinUI.vcxproj` for x64 Debug or Release with MSBuild or Visual Studio. The release workflow copies the self-contained WinUI payload beside the JUCE host.

The WinUI shell is a packaged desktop app. During development, launch it with its package registration or `winapp run`; do not treat the packaged executable as a standalone unpackaged app.

## VST2 configuration

`LIGHTHOST_ENABLE_VST2` accepts:

- `AUTO` - enables VST2 when usable headers are available;
- `ON` - requires VST2 and fails configuration if headers are unavailable;
- `OFF` - builds without VST2 hosting.

`LIGHTHOST_VST2_PROVIDER` accepts `XAYMAR` or `LEGACY`. A legacy SDK root can be supplied through `LIGHTHOST_VST2_SDK_DIR` or the matching environment variable.

VST2 availability at compile time is separate from the runtime **Enable VST2 plugins** setting.

## Run and recover locally

```powershell
& ".\out\build\windows-vs2022\LightHostModern_artefacts\Release\LightHostModern.exe"
& ".\out\build\windows-vs2022\LightHostModern_artefacts\Release\LightHostModern.exe" --debug
& ".\out\build\windows-vs2022\LightHostModern_artefacts\Release\LightHostModern.exe" --safe-mode
```

See [Persistence and recovery](persistence-and-recovery.md) for every recovery option.

## Create release artifacts

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Release.ps1"
```

Outputs:

```text
out\release\LightHostModern-1.4.0-Setup.msi
out\release\LightHostModern-Setup.msi  # identical compatibility alias
out\release\LightHostModern-Portable.zip
```

The MSI installs under `%ProgramFiles%\LightHostModern`. It uses a stable `UpgradeCode` and a major-upgrade relationship so newer MSI versions replace older ones. During installation it also detects the `InstallLocation` registered by the legacy per-user setup, removes that obsolete payload, and clears its duplicate uninstall entry.

Release builds configure `LIGHTHOST_REALTIME_AUDIT=OFF`; allocation auditing is reserved for explicit validation builds. `-SkipBuild` reuses already-built outputs, which must match the source and version being released. `-SkipTests` skips CTest for explicitly requested local packaging; normal release builds run it.

The portable ZIP contains the same self-contained payload without an executable wrapper. Users extract it to a stable folder and launch `LightHostModern.exe`; no embedded script, temporary extraction, or PowerShell process is used.

Public releases can be Authenticode-signed by setting `LIGHTHOST_SIGNING_THUMBPRINT` to the thumbprint of a trusted code-signing certificate installed in the current user's certificate store. The build signs the host, WinUI executables, and MSI with SHA-256 and a timestamp. Unsigned local builds remain supported but produce an explicit warning and should not be published as official artifacts.

## Application updates

The WinUI shell checks the repository's latest GitHub release over HTTPS. Settings offers a newer version using the MSI for installed copies or the ZIP for portable copies. Downloads are streamed with progress and cancellation, require the published `sha256:` digest, and validate the internal package identity, version, and architecture. MSI updates use a helper that waits for durable session shutdown before starting Windows Installer. Portable updates reveal the validated ZIP for manual extraction. See [Update contract](update-contract.md).

## Repository layout

```text
Source\                         JUCE host, audio engine, IPC, tray, plugins
WinUI\LightHostModern.WinUI\          Native C++/WinRT WinUI 3 shell
WinUI\LightHostModern.WinUI\Locales\ Runtime JSON translation catalogues
Icon\                           Host and application icon assets
ThirdParty\                     Compatibility shims and third-party notices
Utilities\Build Windows.ps1     Host build helper
Utilities\Build Release.ps1     MSI and portable release builder
docs\                           User and internal architecture documentation
```

## Legacy installer cleanup

MSI major upgrades run after InstallInitialize so rollback covers removal of the previous MSI. Cleanup of the old per-user EXE payload runs as the installing user only after commit. It verifies the HKCU product identity, the old executable version/product resource and non-reparse paths. A generated allowlist limits cleanup to known payload paths. Each file is copied and hash-verified under `%LOCALAPPDATA%/LightHostModern/Migrations` before removal. Unknown files and user settings are retained; directories are removed only when empty. Shortcut targets are checked, and startup registration is migrated only when it points to the exact old executable.

The helper does not guess orphan directories or remove another user's installation. Failures are recorded in the migration backup's log. Actual EXE-to-MSI upgrades, cancellation, files in use and MSI repair/uninstall must be exercised in a disposable Windows machine; package inspection alone does not prove that lifecycle.

The allowlist also includes `Uninstall-LightHostModern.ps1`, which the 1.2.x installer created after extracting its application ZIP. It is backed up and removed as an owned file, never executed.
