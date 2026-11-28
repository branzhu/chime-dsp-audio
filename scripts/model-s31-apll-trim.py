#!/usr/bin/env python3
"""Offline S31 APLL SDM planning and TX counter arithmetic, never writes HW.

Fixed /2 APLL->MCLK, /4 MCLK->BCLK, /64 BCLK->WS. These are the
candidate nominal clock-plan assumptions, not measured board configuration.
One SDM register write being possible does NOT prove glitch-free PLL retuning.
"""
from fractions import Fraction
import json

XTAL_HZ = 40_000_000
ODIV = 6
MCLK_DIV = 2
BCLK_DIV = 4
FRAME_BITS = 64
FS_HZ = 48_000
COUNTER_MOD = 1 << 31


def nearest_positive(value):
    assert value >= 0
    return (2 * value.numerator + value.denominator) // (2 * value.denominator)


def plan(ppm):
    desired_fs = Fraction(FS_HZ) * (1 + Fraction(ppm, 1_000_000))
    apll_target = desired_fs * MCLK_DIV * BCLK_DIV * FRAME_BITS
    sdm = nearest_positive((apll_target * (2 * (ODIV + 2)) / XTAL_HZ - 4) * 65536)
    assert 0 <= sdm < (1 << 22)
    vco = Fraction(XTAL_HZ) * (4 + Fraction(sdm, 65536))
    assert 350_000_000 <= vco <= 500_000_000
    apll = vco / (2 * (ODIV + 2))
    fs = apll / (MCLK_DIV * BCLK_DIV * FRAME_BITS)
    achieved_ppm = (fs / FS_HZ - 1) * 1_000_000
    return {
        'requested_ppm': ppm,
        'sdm_packed': sdm,
        'sdm_hex': f'0x{sdm:06x}',
        'sdm2': sdm >> 16, 'sdm1': (sdm >> 8) & 255, 'sdm0': sdm & 255,
        'apll_model_hz': float(apll), 'fs_model_hz': float(fs),
        'achieved_ppm': float(achieved_ppm),
        'error_ppm': float(achieved_ppm - ppm),
    }


def counter_delta(previous, current):
    """Valid only when fewer than 2**31 increments occurred between samples."""
    assert 0 <= previous < COUNTER_MOD and 0 <= current < COUNTER_MOD
    return (current - previous) % COUNTER_MOD


def analyze():
    step_hz = Fraction(XTAL_HZ, 65536 * 2 * (ODIV + 2) * MCLK_DIV * BCLK_DIV * FRAME_BITS)
    step_ppm = step_hz / FS_HZ * 1_000_000
    rows = [plan(p) for p in range(-2000, 2001)]
    assert all(abs(r['error_ppm']) <= float(step_ppm / 2) + 1e-9 for r in rows)
    assert all(a['sdm_packed'] <= b['sdm_packed'] for a, b in zip(rows, rows[1:]))
    assert counter_delta(COUNTER_MOD - 20, 12) == 32
    assert counter_delta(123, 456) == 333
    # Both counters below are hardware wire/FIFO-domain units, NOT media frames.
    bclk_hz = FS_HZ * FRAME_BITS
    fifo_words_hz = FS_HZ * 2
    return {
        'scope': 'HOST_ARITHMETIC_ONLY_NO_HW_REGISTER_WRITES_OR_GLITCH_GUARANTEE',
        'assumptions': {'xtal_hz': XTAL_HZ, 'o_div': ODIV, 'apll_to_mclk_div': MCLK_DIV,
                        'mclk_to_bclk_div': BCLK_DIV, 'bits_per_stereo_frame': FRAME_BITS},
        'sdm_step_fs_hz': float(step_hz), 'sdm_step_ppm': float(step_ppm),
        'max_abs_error_ppm_over_1ppm_grid_2000ppm': max(abs(r['error_ppm']) for r in rows),
        'sample_plans': [plan(p) for p in (-2000, -300, -100, 0, 100, 300, 2000)],
        'counter_modulus': COUNTER_MOD,
        'bclk_wrap_seconds_at_nominal': COUNTER_MOD / bclk_hz,
        'fifo_wrap_seconds_at_nominal_stereo': COUNTER_MOD / fifo_words_hz,
        'cautions': ['VCO range check is static only; no analog loop stability model',
                     'No check of silicon live SDM latch/calibration/settling behavior',
                     'FIFO and BCLK register reads are not a simultaneous snapshot',
                     'FIFO counter leads wire output; lane media progress needs metadata',
                     'Disable ETM counter-reset tasks and FIFO supplementation for this observation model'],
    }


if __name__ == '__main__':
    print(json.dumps(analyze(), indent=2))
