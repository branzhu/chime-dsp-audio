# Captured LDAC phone failures

These are exact fault-time post-AVDTP bytes, including the media header. They
are not encoder fixtures and have not been repaired. PROVENANCE.json pins the
bytes, source metadata and SHA-256. The second sample has a readable offset/hex
transcription from the user-pasted UART dump, not an original UART log file.

Both packets are 657 bytes, with valid headers at offsets1,329,493 and an invalid
header at165. The first has 3f:32:80 there; the second has 15:22:60. Never treat
the invalid header's computed length as permission to skip/resync/play it.

The worker regression rejects each whole packet before any PCM and checks exact
fault capture. test_ldac_phone_failures.c separately exercises the actual native
core with isolated nominal frame slots, precise errors and cold/warm-up cases.
Synthetic corrected-header copies are test controls only; the fixture bytes on
disk remain unchanged. See docs/bluetooth-ldac-captured-failure-2026-11-21.md.

No ingress snapshot, HCI snoop or real preceding phone decoder history is present.
These captures identify rejection points, not the layer that produced bad bytes.
