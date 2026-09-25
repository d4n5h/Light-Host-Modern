# Audio

The Audio page configures the stream owned by the host process. Changes are sent to the host over IPC and are reflected back through the next state snapshot.

## Backend and device selection

Select an available backend first. The list is provided by JUCE and commonly includes Windows Audio, DirectSound, and ASIO when those drivers are available.

- For Windows Audio and DirectSound, input and output devices can be selected independently.
- For ASIO, **Device** represents one driver. LightHostModern validates that the driver actually opens and avoids saving an invalid mixed input/output pair.

If a selection fails, the host keeps or restores the previous working setup and returns a descriptive error to the UI.

## Input and output channels

The page is ordered Devices, Format, Input/Output settings, then Input/Output channels. With sufficient width, input stays on the left and output on the right in both rows. Each settings card contains its mono switch and Individual/Pairs selector, with controls aligned to the right. Each channel card contains its title, Check all / Uncheck all action and list. Narrow windows stack each direction's settings and channels.

The input and output sections each offer **Individual / Pairs** selection. Inputs default to Individual and outputs to Pairs. Switching this view preserves enabled channels and does not restart audio. Pairs follow physical channel order (1 + 2, 3 + 4); an odd last channel remains individual. The selector is hidden when fewer than two channels exist.

A partially checked pair has only one channel enabled. Clicking it enables both channels in one transaction; clicking a fully checked pair disables both. **Check all / Uncheck all** still changes the complete section. Presentation preferences are saved separately for inputs and outputs for each backend/input/output device combination.

Channel masks are saved for the combination of backend, input device, and output device. Returning to the same configuration restores its previous channel choices when possible.

## Sample rate and buffer size

The available sample rates and buffer sizes come from the active driver. Changing either value rebuilds the device setup and prepares the running plugin chain for the new stream configuration.

A smaller buffer can reduce latency but gives the realtime thread less time to finish each block. A larger buffer is normally more tolerant of expensive plugins but increases monitoring latency.

## Signal flow

The enabled input channels enter the serial running chain. Each plugin receives the output of the preceding slot, and the result is routed to the enabled output channels. Empty and fully bypassed chains preserve direct input-to-output routing where the channel configuration allows it.

See [Audio processing](audio-processing.md) for the realtime implementation.

## Disabled choices

Settings can block specific backends or device choices. Disabled choices are excluded from automatic recovery and cannot be selected manually until they are enabled again through **Manage enabled devices**.

## Mix inputs to mono

This switch sits in **Input settings**, is off by default and is saved for the exact backend/input-device/output-device combination. Existing saved choices are preserved. It sums the enabled, packed input channels with unity gain before the plugins, routing that sum into the main stereo pair. Other host channels retain their route. The sum can exceed 0 dBFS or cancel signals of opposite polarity; there is no normalization or limiter.

A 5 ms input-matrix transition avoids a forced fade to silence. Both global and plugin dry paths receive the same transformed input. A plugin with a mono main output is centered in the main stereo route while mixing is enabled; real stereo output is preserved. Auxiliary plugin buses remain isolated.

Dashboard and Audio show a notice when processing is unavailable. Explicit None/safe mode and an unconfigured first start are distinguished from a missing selected device. Retry bounds remain 1–60 seconds and 1–100 attempts. If the open device stops delivering callbacks for 2 seconds, the same notice says the host is reopening that device. A plugin that stays inside the audio callback is reported instead, and the device is not closed.

## Main output to mono

This independent switch sits in **Output settings**. When physical outputs 1 and 2 are both enabled, each receives their average (`0.5 × L + 0.5 × R`) after plugins, global bypass and mute, before output metering. Switching uses a 5 ms transition. Equal signals keep their amplitude; opposite polarities can cancel. There is no limiter or normalization.

If only one principal output is enabled, its signal and level are preserved. Outputs 3 and higher are unchanged, even if they are the only enabled outputs. The tooltip explains when the main pair is incomplete. The preference defaults off, is saved per device combination and can be prepared while a configured device is temporarily unavailable. Both mono switches are independent of channel grouping.
