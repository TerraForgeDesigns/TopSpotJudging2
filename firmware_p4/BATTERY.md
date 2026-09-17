# Handheld battery monitor

The monitor is independent of judging, networking, sync, show state and storage.
`app_main` starts it after the initial UI is visible and outside the LVGL lock.
An initialization failure is logged and leaves the battery indicator unavailable.

## ADC and voltage

- Verified board divider: BAT -> R92 (200 kohm) -> GPIO20 -> R93 (100 kohm) -> GND.
- ESP32-P4 GPIO20 is ADC1 channel 4. Initialization resolves the GPIO through
  `adc_oneshot_io_to_channel` and checks this mapping before claiming the unit.
- Oneshot mode, 12-bit resolution, 6 dB attenuation, default ADC clock, ULP disabled.
  A 4.2 V battery produces about 1.4 V at the ADC input.
- ESP-IDF 5.5.5 curve-fitting calibration with matching unit/channel/attenuation/
  bit width converts each raw reading to millivolts. There is no uncalibrated fallback.
- `raw_voltage_mv = calibrated_filtered_adc_mv * (200000 + 100000) / 100000`
  (equivalently, `adc_mv * 3`). The divider ratio remains exactly 3.0.
- `voltage_mv = clamp(raw_voltage_mv + BATTERY_VOLTAGE_OFFSET_MV, 3000, 4200)`.
  `BATTERY_VOLTAGE_OFFSET_MV` is **163** in `main/battery_model.c`.
  Both SOC and displayed voltage use this corrected millivolt value.

The installed IDF mapping is in `components/soc/esp32p4/include/soc/adc_channel.h`.
API references: [oneshot](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/peripherals/adc_oneshot.html)
and [calibration](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/peripherals/adc_calibration.html).

## Exact battery pipeline and correction rationale

The original implementation had no correction. Its actual order was calibration,
averaging, smoothing, then divider scaling (not scaling before smoothing).
This change adds only correction/clamping between scaling and SOC conversion:

| Stage | File, function and value |
|---|---|
| Startup/configuration | `main/main.c:app_main` calls `main/p4_battery.c:top_spot_battery_init` after releasing the initial LVGL lock. `s_adc`, `s_channel`, `s_cali` own the unchanged oneshot/calibration configuration. |
| Raw ADC | `p4_battery.c:read_average_mv` discards one `adc_oneshot_read` result, then collects 32 retained `raw` readings, 10 ms apart. |
| Calibration | In that same loop, `adc_cali_raw_to_voltage(s_cali, raw, &mv)` converts each reading to calibrated divider millivolts. |
| Average | `sum` accumulates calibrated `mv`; `*average_mv = (sum + 16) / 32` rounds the average to millivolts. |
| Smoothing | `p4_battery.c:battery_task` passes `average_mv` to `battery_model.c:top_spot_battery_filter_mv(&filter, average_mv)`. EMA alpha is 1/4, with fractional `filter.adc_mv_q8` state. Output is rounded to `next.adc_mv`. |
| Divider scaling | `battery_model.c:top_spot_battery_voltage_mv(next.adc_mv)` multiplies by exactly 3, yielding the local `raw_voltage_mv`. Here "raw voltage" means calibrated, averaged, filtered and scaled, but uncorrected; it is not raw ADC counts. |
| Correction (new) | `battery_model.c:top_spot_battery_correct_voltage_mv(raw_voltage_mv)` adds 163 mV and clamps to 3000-4200 mV, returning `next.voltage_mv`. |
| SOC and warnings | `battery_model.c:top_spot_battery_percent(next.voltage_mv)` produces `next.percent` using the unchanged table. `top_spot_battery_level(next.percent)` produces `next.level`, retaining inclusive 20/10/5% thresholds. |
| Cache publication | `p4_battery.c:battery_task` publishes the complete `next` structure to `s_status` inside `s_lock`. This happens once per 2-second sampling cycle. |
| UI snapshot | `ui_screens.c:battery_status_timer_cb`, every 1000 ms, calls `update_battery_status_label`. That function calls `top_spot_battery_get_status(&battery)` once; the getter copies `s_status` under the same lock. |
| Displayed voltage | The unchanged label updater computes `voltage_cv = (battery.voltage_mv + 5) / 10`, then formats `voltage_cv / 100` and `voltage_cv % 100` to two decimal places. |
| Displayed percentage | The same label call formats `battery.percent`, with the existing icon, warning suffix and color from `battery.level`. |

Voltage and SOC therefore originate from the same corrected filtered sample.
Percentage is recalculated on every successful batch from the underlying
millivolt value, before display rounding; it is not computed from the two-decimal
text. There is no separate percentage cache/update schedule that can lag behind
voltage. Smoothing and the 1-second UI timer delay both together. Failed batches
publish invalid data and reset smoothing; the UI shows unavailable data.

The supplied battery-only pairs are `x = handheld`, `y = multimeter`, in volts:
`(3.870, 4.022)`, `(3.830, 3.990)`, `(3.890, 4.066)`.
Equal-weight least squares gives:

- Additive model `y_est = x + b`: `b = mean(y - x) = 0.162666667 V`.
- Scale-only model `y_est = a*x`: `a = sum(x*y) / sum(x*x) = 1.042111845`.

Residual is corrected estimate minus multimeter:

| Pair | Best offset estimate (V) | Offset residual (mV) | Best scale estimate (V) | Scale residual (mV) | Implemented +163 mV (V) | Implemented residual (mV) |
|---|---|---|---|---|---|---|
| 1 | 4.032667 | +10.667 | 4.032973 | +10.973 | 4.033 | +11 |
| 2 | 3.992667 | +2.667 | 3.991288 | +1.288 | 3.993 | +3 |
| 3 | 4.052667 | -13.333 | 4.053815 | -12.185 | 4.053 | -13 |

RMS residual: additive 9.978 mV, scale 9.496 mV; the scale improvement is only
0.482 mV. Input handheld readings are rounded to 10 mV and span only 60 mV,
so this does not meaningfully establish a gain error. The integer additive
offset is the simpler empirical deadline correction; the constant is the fitted
offset rounded to 1 mV, not a claim of 1 mV measurement accuracy. Changing the
verified divider ratio is unjustified. Adding after smoothing avoids altering
the filter's state or startup behavior. No specific hardware root cause is
assumed, and accuracy at substantially lower battery voltages remains unverified.

**USB limitation:** There is no reliable software VBUS detector on the current
board. This normal-operation path applies the correction without automatic mode
selection. It is calibrated only for battery-only operation. With USB connected,
even without a battery installed, voltage and SOC remain unreliable and may
clamp to 4.20 V / 100%. No charging, USB-presence or battery-presence inference
is implemented.

## Percentage estimate

The generic single-cell Li-ion curve uses these voltage/percentage knots:

| V | % |
|---|---|
| 3.30 | 0 |
| 3.50 | 5 |
| 3.60 | 10 |
| 3.70 | 20 |
| 3.75 | 30 |
| 3.80 | 40 |
| 3.85 | 50 |
| 3.90 | 60 |
| 3.95 | 70 |
| 4.00 | 80 |
| 4.10 | 90 |
| 4.20 | 100 |

Interpolate between adjacent knots, round to the nearest integer, and clamp to
0–100%. This is a voltage estimate, not a measured fuel-gauge state of charge.
The actual battery, load, temperature, ageing and charging can shift the estimate.
The monitor does not infer charging or battery presence from ETA6098 or voltage.

## Timing and UI

A priority-1 FreeRTOS task with a 3072-byte stack samples every 2 seconds. Each
batch discards the first conversion and averages 32 calibrated readings spaced
10 ms apart. An exponential moving average (alpha 1/4, fractional internal state)
smooths the result, with immediate initialization from the first valid batch.

An independent LVGL timer runs every second and copies only a cached snapshot
under a short critical section; ADC calls, delays and logging stay in the worker.
The existing status bar shows an LVGL battery symbol, percentage and voltage. The camera
header uses its spare lower edge so its existing row retains its space.

- 21–100%: normal text.
- 11–20%: amber `Low`.
- 6–10%: red `Very low`.
- 0–5%: red `CRITICAL`.
- Before the first reading, or on calibration/sampling failure: muted `--%  --.--V`.

No warning blocks input or changes navigation. Failed batches are discarded;
the next successful batch reinitializes smoothing. Successful readings log at
startup/recovery and every 30 seconds:

```text
BATTERY: adc_mv=1290 raw_voltage=3.870V corrected_voltage=4.033V percent=83
```

## Local validation

`tests/test_battery_model.py` compiles and executes the production C model using
Python's standard library. It covers divider conversion, all curve knots,
interpolation, clamping/monotonicity, warning boundaries, smoothing, noise
reduction, fractional convergence, filter restart, the three supplied measurement
pairs, corrected-voltage clamps and corrected SOC. A source integration guard
also checks worker-to-UI use of the same corrected snapshot. Windows requires a
host-capable Clang (the ESP-only Clang cannot target x86) and LLD:

```powershell
$env:BATTERY_TEST_CLANG = 'C:/Program Files/Unity/Hub/Editor/6000.4.6f1/Editor/Data/PlaybackEngines/WebGLSupport/BuildTools/Emscripten/llvm/clang.exe'
$env:BATTERY_TEST_LLD = 'C:/Espressif/tools/esp-clang/esp-19.1.2_20250312/esp-clang/bin/lld.exe'
python tests/test_battery_model.py
```

On Linux/macOS use `python3 tests/test_battery_model.py` with `CC` set if needed.
Build the complete firmware with `idf.py build` from an ESP-IDF 5.5.5 environment.
If the compiler cache stalls in the restricted local environment, use
`CCACHE_DISABLE=1` and set `CCACHE_DIR` to `build/battery_ccache` for that process.

No flashing is part of this change. Future authorized hardware checks should
compare logged BAT voltage to a multimeter, check stability under handheld load,
and verify the header appearance and all warning states. Host tests do not
exercise the ADC hardware, calibration eFuses, RTOS timing or display rendering.

Validation of the +163 mV correction: all 12 focused tests passed (11 compiled-C
model tests and one worker/UI source integration guard). The full ESP-IDF 5.5.5
build passed, including application and bootloader size checks; the application
image is 1,790,032 bytes, with 79% of the 8 MiB app partition free. This build
reported no compiler warnings. No device was flashed. Existing main-directory
file hashes confirmed changes only to the four battery module/model files;
UI, camera, networking, sync, storage, judging and show-state code were unchanged.
Build log: `build/log/idf_py_stdout_output_31192`.
