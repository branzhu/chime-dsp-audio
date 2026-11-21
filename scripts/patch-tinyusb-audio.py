"""Generate a pinned, auditable TinyUSB audio class fix; never edit managed sources."""
from pathlib import Path
import hashlib
import sys

UPSTREAM_SHA256 = "347ac32ce077f8b34b6b21069afb1959f6eb89d8dc32d80c4a672a19ecabc8bc"

def patched(source):
    if hashlib.sha256(source).hexdigest() != UPSTREAM_SHA256:
        raise ValueError("TinyUSB audio_device.c changed: review/rebase Chime startup fixes")
    text = source.decode("utf-8")
    changes = [
        ("TU_VERIFY(0 < tu_fifo_write_n(&audio->ep_out_ff, audio->lin_buf_out, n_bytes_received));",
         "if (n_bytes_received) {\n    TU_VERIFY(0 < tu_fifo_write_n(&audio->ep_out_ff, audio->lin_buf_out, n_bytes_received));\n  } // A legal ZLP must still rearm OUT and keep feedback alive."),
        ("// Schedule first feedback transmit\n            audiod_fb_send(func_id, false);",
         "// Chime: prepare nominal feedback BEFORE the first queued packet.\n            TU_VERIFY(audiod_fb_params_prepare(func_id, alt));\n            audiod_fb_send(func_id, false);"),
        ("#if CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP\n      // Prepare feedback computation parameters\n      TU_VERIFY(audiod_fb_params_prepare(func_id, alt));\n#endif// CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP",
         "// Feedback parameters were prepared before its first transfer."),
        ("audio->feedback.compute.fifo_count.nom_value = nominal;",
         "audio->feedback.compute.fifo_count.nom_value = nominal;\n        audio->feedback.value = nominal; // Seed without waiting for the first OUT data.")
    ]
    for before, after in changes:
        if text.count(before) != 1:
            raise ValueError("TinyUSB startup patch context mismatch")
        text = text.replace(before, after)
    return text.encode("utf-8")

if __name__ == "__main__":
    src, dst = map(Path, sys.argv[1:])
    result = patched(src.read_bytes())
    dst.parent.mkdir(parents=True, exist_ok=True)
    if not dst.exists() or dst.read_bytes() != result:
        dst.write_bytes(result)
    print("PASS pinned TinyUSB audio startup-v3: nonzero first feedback + ZLP OUT rearm")
