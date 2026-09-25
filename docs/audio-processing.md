# Audio processing

LightHostModern uses JUCE's `AudioDeviceManager` and `AudioProcessorPlayer` with a custom `RealtimeHostProcessor`. Each mixer strip has its own plugin chain. Strips sum into the selected outputs, then the master volume and mute are applied.

## Chain snapshots

The message thread builds a `ChainSnapshot` containing:

- current sample rate and block size;
- enabled input/output channel counts;
- maximum plugin channel requirement;
- total latency;
- ordered shared plugin slots.

The completed snapshot is published atomically to the realtime processor. The audio callback reads one immutable snapshot for the entire block, so reorder, duplicate, removal, bypass, or device changes do not mutate its structure halfway through processing.

Old snapshots are retired and collected outside the callback. Compatible plugin slots are reused during rebuilds to avoid destroying and recreating processors unnecessarily.

## Processing a block

For every audio block:

1. The input peak is calculated.
2. Enabled inputs are packed by JUCE. If mono mixing is enabled for the current device pair, their unity-gain sum is faded into the main stereo pair over 5 ms, before dry/bypass capture and plugins.
3. Each strip gathers its inputs, runs its slots, applies its fader, and is delayed to the slowest strip. A pan value other than center then balances the first two outputs. A one-channel strip uses constant-power pan. A wider strip keeps center at unity and turns one side down.
4. Bypassed slots pass audio through their compensation path. Global bypass does the same for every slot, so strip routing and faders stay active.
5. Failed slots are disabled for later blocks.
6. The strips are summed, the master volume is applied, then global mute.
7. If enabled and both physical principal outputs are active, output mono blends their signals toward their average over 5 ms. Auxiliary outputs and lone principal outputs retain their signal.
8. The stream-resume gain is applied, and output meters measure only actual output channels before JUCE sends them to the device.

The empty-chain path still routes compatible inputs to outputs instead of producing silence.

## Channel handling

Plugins can expose mono, stereo, or other channel layouts. The scratch buffer supports up to 256 channels, and each slot records its input/output capabilities. The host adapts between the opened device and plugin requirements rather than assuming every processor is stereo. With mono input mixing enabled, a mono plugin's main output is centered in the main stereo pair; true stereo output and auxiliary bus handling retain their existing behavior. Summing inputs can exceed 0 dBFS; there is no automatic normalization or limiter.

Plugins that expose no usable audio input/output configuration can be rejected when added to the running chain.

At device start, the active physical output mask maps outputs 1/2 to JUCE's packed buffer positions. This map and the output count are prepared while the callback is excluded. Output mono reads an atomic preference; it adds no callback allocation, device enumeration, settings access or lock. The transition continues from its current blend when toggled rapidly. Global bypass keeps this final monitoring transformation active; global mute still silences it. Input meters remain ahead of input mixing, and output meters include output mono even with Diagnostics disabled.

## Bypass and latency

Each slot stores the latency reported by its processor. When bypassed, a delay buffer passes the dry signal with equivalent latency. This keeps downstream timing aligned and avoids changing total chain latency merely because an effect was bypassed.

The Dashboard reports the sum of active slot latencies in samples.

## Realtime safety

The callback avoids settings writes, UI work, plugin database mutation, and normal logging. Process exceptions are caught at the slot boundary, recorded atomically, and reported later from a non-realtime timer. Rebuilds, state saves, retired snapshot cleanup, and diagnostic logging run away from the callback.

This design reduces realtime-thread risk, but plugin code executes in-process. A plugin that performs an unrecoverable native fault can still crash the host.
