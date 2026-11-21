English | [简体中文](safety.zh-CN.md)

# Safety and known limitations

## Audio and electrical safety

- This is development-board firmware, not a fully certified product. Before first power-on, check power supplies, shorts, grounding, cooling, and loads.
- Keep the two Sub BTL outputs independent. Never connect amplifier outputs directly in parallel, and never ground either BTL terminal as though it were a single-ended output.
- The current firmware has no fixed USB 1/8 attenuation. Amplifier digital volume is 0 dB, analog gain is −8 dB, and the DSP uses the project's compiled gains. Output levels may change substantially when upgrading from older firmware: greatly reduce computer / phone volume first.
- Local mute / standby and STOP are software controls, not power isolation or emergency-stop devices. They do not replace hardware protection.
- Verified speaker power limiting, subsonic protection, full-load thermal protection, and click-free switching are not claimed.

## Networking and wireless

The current firmware is intended for bench use on a trusted LAN:

- The SigmaStudio TCP bridge listens on port 8086 without client authentication. Connecting can affect DSP ownership and normal playback. Do not expose it to the Internet or configure router port forwarding.
- Web Origin / request-header checks are not user authentication. The default provisioning AP password, `chime-setup`, is a fixed development setting, not a device-unique secret.
- Bluetooth uses Just Works. While disconnected, the device can be discovered / connected to; a connection selects the Bluetooth source. Do not assume a physically authorized pairing window exists.
- Do not share raw logs containing passwords, device addresses, private recordings, or personal paths. Do not compile credentials into public firmware.

## Known issues and validation gaps

1. **High priority**: synchronous TCP sends in the control owner can delay STOP handling and amplifier-protection polling under backpressure; the current send deadline can reach 30 seconds. Network I/O and safety control still need separation and combined-fault testing.
2. Initialization failures in auxiliary network services can still stop local audio entirely. Failure isolation needs improvement.
3. Bluetooth rebuffer recovery can produce waveform discontinuities / clicks. De-click handling and board tests are still needed.
4. Host ASan / malformed-frame regressions do not prove RF throughput, RTOS scheduling, phone interoperability, or long-term stability.
5. Redistribution permissions for imported LDAC / reference adapter code and codec patent rights remain unresolved. A successful build is not permission to redistribute; see [Licensing](licenses.md).

Earlier board-level operation of the built-in SBC path does not validate every codec, firmware revision, source transition, power level, or safety scenario in the current version.
