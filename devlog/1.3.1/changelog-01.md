# Changelog 01 — compact width and release 1.3.1

## Added

- None.

## Removed

- None.

## Improved

- App, updater user-agent, manifests, executable resources and package metadata advance to 1.3.1.

## Fixed

- Increased the Compact content width from 780 to 1000 device-independent pixels. A shared constant controls the centered page insets and dashboard responsive calculations.
- The width change preserves header/card/toolbar alignment, smaller-window responsiveness and window-edge scrollbars.

## Validation context

The preceding compact-width task compiled the change and inspected the portable UI. Settings, Audio and Plugins each measured 1000 DIPs at 200% scaling; four screenshots were visually reviewed. Release 1.3.1 package checks are recorded separately after packaging.
