# Tray and window behavior

The JUCE host owns application lifetime and can continue processing audio without keeping the WinUI window open.

## Notification-area icon

The icon is created by the host process. Its native context menu contains:

- **Open app UI** - launches, restores, or focuses the WinUI shell;
- **Mute output** - toggles the host-owned output mute;
- **Bypass chain** - toggles latency-compensated global bypass while processors continue running;
- **Quit** - saves state, closes the shell, stops audio processing, and exits the host.

The selected Color, White, or Black icon variant is applied to both host and interface assets where supported.

## Opening the interface

The host first locates the canonical `WinUI/x64/Release/LightHostModern.WinUI` shell inside its own payload and launches it with the unique host pipe name. Development fallbacks must identify the repository. An existing window for the same profile is restored and activated instead of launching another shell. Temporary profiles include their identifier in the window title and tray tooltip.

The shell is a view of the host state. Closing or recreating it does not rebuild the audio engine by itself. Mute and global bypass both start off in a new host and survive closing/reopening only the shell. The initial window is sized for the current DPI and kept inside the monitor work area.

## Close to tray

When enabled, closing the WinUI window leaves the host, audio device, plugin instances, and notification-area icon active. Reopen the interface from the tray menu.

When disabled, the close flow requests host shutdown, which saves plugin state and flushes pending settings before exiting.

Windows taskbar **End task** is treated as ending the whole app, regardless of Close to tray. The host detects an abrupt UI exit and shuts down; this also applies to UI crashes. If plugin code blocks cleanup, a ten-second fallback terminates the host. Normal window closing acknowledges a separate per-launch event and continues to honor Close to tray.

## Start with Windows

The setting creates a current-user Windows `Run` entry named `LightHostModern`. It points to the host executable that enabled the option. Disabling the setting removes the value.

Portable users should extract the complete ZIP to a stable folder before enabling startup because the registration follows the extracted host executable. Moving or deleting that folder invalidates the startup entry; reopening the app and toggling the option off and on registers its new location.

Temporary test profiles reject changes to Windows startup registration and do not open audio automatically.

## Navigation and responsive layout

The sidebar contains Dashboard, Audio, Plugins, optional Diagnostics, optional Support me, and Settings, in that order. Its collapsed state keeps the app logo and accessible navigation icons visible. The bottom control expands or collapses the pane.

**Settings > Appearance > Sidebar on open** chooses Collapsed (default) or Expanded for each new window, including reopening from the tray. Manual toggling changes only the current window. Restoring or focusing an existing window preserves its current state.

Pages are created on first access and retained. Compact mode limits content width; Expanded mode uses the available space. Running and Installed each have a bounded virtualized list with its own scrolling. Search and visual sorting preserve the actual processing order; drag reordering is available only in the unfiltered chain-order view.
