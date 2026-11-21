# SigmaDSP Wireshark dissector

`sigmadsp.lua` passively decodes the observed SigmaStudio TCP framing and ADAU145x register operations. It does not connect to a board, change DSP state or certify protocol compatibility.

## Use

For a one-off offline decode, without installing the plugin:

```sh
tshark -n -2 -X lua_script:tools/wireshark/sigmadsp.lua -r capture.pcapng -Y sigmadsp
```

Alternatively, copy the Lua file to the **Personal Lua Plugins** directory shown by Wireshark (Help / About / Folders), then reload Lua plugins. Do not load the same dissector twice. TCP 8086 is the default bench port; use Decode As when necessary.

The parser exposes commands, chip/address/data fields, register names, request/response matching and malformed-frame diagnostics. TCP ordering/reassembly is handled by Wireshark. Captures may contain private device data; sanitize them before sharing.

## Tests

```sh
node scripts/test-sigma-wireshark.mjs
```

Set `TSHARK` to the executable if it is not on PATH (the Windows default path is also recognized). Tests create synthetic local captures; no hardware traffic is collected. The independent register facts and minimal loopback protocol fixtures are in `software/tests/fixtures/sigma/`.

Previously verified with TShark 4.6.9 / Lua 5.4. Other versions require validation. For firmware and network safety limits see [the safety guide](../../docs/safety.md); the bench TCP bridge has no client authentication and must not be exposed to the Internet.
