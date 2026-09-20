#!/usr/bin/env python3
"""Source-level guard for the build-local ESP-IDF DWC HCD diagnostic patch."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_hcd_dwc_diag", ROOT / "platform/adv/patch_hcd_dwc_diag.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)

fixture = (
    "prefix\n"
    + patch.STATE_ANCHOR
    + "middle\n"
    + patch.SKIP_ANCHOR
    + "suffix\n"
)
patched = patch.patch_source(fixture)

assert "hcd_dwc_t017_isoc_diag_snapshot" in patched
assert "++s_t017_hcd_isoc_diag.total;" in patched
assert "packet_hist[pkt_idx]" in patched
assert "last_start_idx = buffer->flags.isoc.start_idx;" in patched
assert "last_desc_idx = (uint32_t)desc_idx;" in patched
assert "last_stop_idx = buffer->status_flags.stop_idx;" in patched

for invalid in (fixture.replace(patch.SKIP_ANCHOR, ""), patched):
    try:
        patch.patch_source(invalid)
    except ValueError:
        pass
    else:
        raise AssertionError("HCD diagnostic source guard accepted invalid input")

cmake = (ROOT / "platform/adv/CMakeLists.txt").read_text()
assert 'idf_component_get_property(usb_lib usb COMPONENT_LIB)' in cmake
assert 'source_name STREQUAL "hcd_dwc.c"' in cmake
assert 'patch_hcd_dwc_diag.py' in cmake

provider = (ROOT / "platform/adv/adv_audio_uac.cpp").read_text()
assert "hcd_dwc_t017_isoc_diag_snapshot" in provider
assert "T017 HCD pkt[0..11]" in provider

print("ADV HCD ISO diagnostic patch wiring: PASS")
