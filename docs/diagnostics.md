# Diagnostics

Diagnostics appears below Settings in the sidebar and presents local measurements in cards. Each card has an icon, heading, description, and readings below it.

- **Performance:** DSP load and CPU use by the host, interface, and scanner worker.
- **Audio reliability:** xruns, processing failures, and MIDI events dropped after exceeding capacity.
- **Stream format:** requested sample rate and buffer size alongside the driver's actual values.
- **Latency:** plugin-chain latency and driver input/output latency.
- **Processing activity:** processed audio blocks/samples and input/output MIDI event counts.

Visible diagnostic readings refresh at approximately one-second intervals. These measurements are local and are not uploaded.

**Settings > General > Diagnostics** is enabled by default. Turning it off requests confirmation, hides the page, and stops diagnostics collection. Audio processing, plugin scanning, and the Dashboard peak meters continue. Turning it on restores the page and resumes monitoring.
