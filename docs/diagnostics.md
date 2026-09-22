# Diagnostics

Diagnostics appears after Plugins and before Support me in the sidebar and presents local measurements in cards. Each card has an icon, heading, description, and readings below it.

- **Performance:** whole-app CPU, DSP load and CPU use by the host, interface, and scanner worker.
- **App memory:** private resident RAM and committed memory for the app processes.
- **Audio reliability:** xruns, processing failures, and MIDI events dropped after exceeding capacity.
- **Stream format:** requested sample rate and buffer size alongside the driver's actual values.
- **Latency:** plugin-chain latency and driver input/output latency.
- **Processing activity:** processed audio blocks/samples and input/output MIDI event counts.

Visible diagnostic readings refresh at approximately one-second intervals. These measurements are local and are not uploaded.

**Settings > General > Diagnostics** is enabled by default. Turning it off requests confirmation, hides the page, and stops diagnostics collection. Audio processing, plugin scanning, and the Dashboard peak meters continue. Turning it on restores the page and resumes monitoring.

## Resource readings and explanations

App CPU is the sum of the audio host, this interface and scanner processes, normalized to the whole computer. Loaded plugins already belong to host usage. Resident RAM counts private physical pages; committed memory counts private committed bytes. Shared pages are excluded from both process sums. Missing OS measurements show Unavailable rather than zero.

Memory sampling is requested only while Diagnostics is visible, at approximately 1 Hz. The host lease expires after two seconds without requests. Each metric has localized English/Portuguese hover text and accessible help text; numeric values remain keyboard selectable. Independent Dashboard meters continue when Diagnostics is disabled.
