"""Unit-test extracted real driver functions with mocked endpoint/FIFO APIs.
Not a full TinyUSB stack, USB bus, or host simulation.
Run under a compiler environment: python ... --cc cl (or gcc).
"""
from pathlib import Path
import importlib.util
import subprocess
import sys
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("patch",root/"scripts/patch-tinyusb-audio.py")
patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)
raw=(root/"software/managed_components/espressif__tinyusb/src/class/audio/audio_device.c").read_bytes()
source=patch.patched(raw).decode()
try:
    patch.patched(raw+b"\n")
except ValueError:
    pass
else:
    raise AssertionError("unreviewed upstream change accepted")
# Verify initialization runs BEFORE the actual first send, not after the app callback.
start=source.index("if (is_feedback_ep)",source.index("static bool audiod_set_interface(uint8_t rhport, tusb_control_request_t const *p_request) {"))
end=source.index("#else", start)
block=source[start:end]
assert block.index("audiod_fb_params_prepare(func_id, alt)") < block.index("audiod_fb_send(func_id, false)")
assert source.count("TU_VERIFY(audiod_fb_params_prepare(func_id, alt));")==1

def function(signature):
    start=source.index(signature+" {")
    end=source.index("\n}",start)+2
    return source[start:end]
# Keep the real feedback state layout and all three real function bodies.
start=source.index("  struct {\n    uint32_t value;")
end=source.index("  } feedback;",start)+len("  } feedback;")
state=source[start:end]
functions=[function("static bool audiod_fb_params_prepare(uint8_t func_id, uint8_t alt)"),
    function("static void audiod_fb_fifo_count_update(audiod_function_t *audio, uint16_t lvl_new)"),
    function("static bool audiod_rx_xfer_isr(uint8_t rhport, audiod_function_t* audio, uint16_t n_bytes_received)")]
build=root/"tmp/usb-startup-unit";build.mkdir(parents=True,exist_ok=True)
(build/"driver-under-test.inc").write_text("typedef struct {\n"+state+"\n uint8_t ep_fb,ep_out,ep_out_alt;uint16_t ep_out_sz;uint8_t lin_buf_out[196];tu_fifo_t ep_out_ff;\n} audiod_function_t;\nstatic audiod_function_t _audiod_fct[1];\nstatic uint8_t audiod_get_audio_fct_idx(audiod_function_t *a){(void)a;return 0;}\nstatic void audiod_fb_fifo_count_update(audiod_function_t *,uint16_t);\n"+"\n".join(functions))
cc=sys.argv[2] if len(sys.argv)==3 and sys.argv[1]=="--cc" else "cc"
exe=build/("startup.exe" if Path(cc).stem.lower()=="cl" else "startup")
if Path(cc).stem.lower()=="cl":
    args=[cc,"/nologo","/std:c11","/W3",f"/I{build}",str(root/"software/tests/usb/test_tinyusb_audio_startup.c"),f"/Fo{build}/",f"/Fe{exe}","/link","/INCREMENTAL:NO"]
else:
    args=[cc,"-std=c11","-Wall","-Wextra",f"-I{build}",str(root/"software/tests/usb/test_tinyusb_audio_startup.c"),"-o",str(exe)]
subprocess.run(args,check=True,cwd=root)
subprocess.run([str(exe)],check=True,cwd=root)
print("PASS patch hash guard and first-feedback initialization ordering; three extracted driver functions, mocked endpoints/FIFO, not full USB stack")
