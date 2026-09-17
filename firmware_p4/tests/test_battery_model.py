"""Execute the production C model on the host; no ESP32 or third-party Python packages.

Windows: set BATTERY_TEST_CLANG to a host-capable clang.exe and BATTERY_TEST_LLD
to lld.exe. Elsewhere, uses CC (default cc). Run: python tests/test_battery_model.py
"""
import ctypes
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Filter(ctypes.Structure):
    _fields_ = [("initialized", ctypes.c_bool), ("adc_mv_q8", ctypes.c_int32)]


class BatteryModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        output = ROOT / "build" / "battery_tests"
        output.mkdir(parents=True, exist_ok=True)
        source = ROOT / "main" / "battery_model.c"
        names = ["filter_mv", "voltage_mv", "correct_voltage_mv", "percent", "level"]
        if os.name == "nt":
            obj = output / "battery_model.obj"
            library = output / "battery_model.dll"
            subprocess.run([
                os.environ["BATTERY_TEST_CLANG"], "--target=x86_64-pc-windows-msvc",
                "-ffreestanding", "-Wall", "-Wextra", "-Werror", "-c", str(source),
                "-o", str(obj),
            ], check=True)
            subprocess.run([
                os.environ["BATTERY_TEST_LLD"], "-flavor", "link", "/dll", "/noentry",
                f"/out:{library}", *[f"/export:top_spot_battery_{n}" for n in names],
                str(obj),
            ], check=True)
        else:
            library = output / "battery_model.so"
            subprocess.run([
                os.environ.get("CC", "cc"), "-shared", "-fPIC", "-Wall", "-Wextra",
                "-Werror", str(source), "-o", str(library),
            ], check=True)
        cls.lib = ctypes.CDLL(str(library))
        for name in names:
            fn = getattr(cls.lib, f"top_spot_battery_{name}")
            fn.restype = ctypes.c_int
            fn.argtypes = ([ctypes.POINTER(Filter), ctypes.c_int]
                           if name == "filter_mv" else [ctypes.c_int])

    def test_divider(self):
        for adc, bat in [(0, 0), (1100, 3300), (1200, 3600), (1400, 4200)]:
            self.assertEqual(self.lib.top_spot_battery_voltage_mv(adc), bat)

    def test_curve_knots(self):
        for mv, percent in [(3300, 0), (3500, 5), (3600, 10), (3700, 20),
                            (3750, 30), (3800, 40), (3850, 50), (3900, 60),
                            (3950, 70), (4000, 80), (4100, 90), (4200, 100)]:
            self.assertEqual(self.lib.top_spot_battery_percent(mv), percent)

    def test_real_battery_measurements(self):
        # Display-derived inputs have 10 mV resolution. A single fitted offset
        # cannot reproduce all three multimeter readings exactly.
        for measured, meter, expected in [(3870, 4022, 4033), (3830, 3990, 3993),
                                           (3890, 4066, 4053)]:
            corrected = self.lib.top_spot_battery_correct_voltage_mv(measured)
            self.assertEqual(corrected, expected)
            self.assertLessEqual(abs(corrected - meter), 14)

    def test_corrected_voltage_clamps(self):
        for measured, expected in [(-2147483648, 3000), (0, 3000), (2836, 3000),
                                  (2837, 3000), (2838, 3001), (4036, 4199),
                                  (4037, 4200), (4038, 4200), (4200, 4200),
                                  (2147483647, 4200)]:
            self.assertEqual(self.lib.top_spot_battery_correct_voltage_mv(measured), expected)

    def test_soc_from_corrected_voltage(self):
        for measured, uncorrected_soc, corrected_soc in [(3870, 54, 83),
                                                        (3830, 46, 79),
                                                        (3890, 58, 85)]:
            corrected = self.lib.top_spot_battery_correct_voltage_mv(measured)
            self.assertEqual(self.lib.top_spot_battery_percent(measured), uncorrected_soc)
            self.assertEqual(self.lib.top_spot_battery_percent(corrected), corrected_soc)
        self.assertEqual(self.lib.top_spot_battery_percent(
            self.lib.top_spot_battery_correct_voltage_mv(4150)), 100)

    def test_worker_and_ui_share_corrected_value(self):
        # Integration guard: the real worker must correct before deriving SOC,
        # and the UI must consume the same snapshot without another conversion.
        worker = (ROOT / "main" / "p4_battery.c").read_text()
        assignments = [
            "next.adc_mv = top_spot_battery_filter_mv(&filter, average_mv);",
            "int raw_voltage_mv = top_spot_battery_voltage_mv(next.adc_mv);",
            "next.voltage_mv = top_spot_battery_correct_voltage_mv(raw_voltage_mv);",
            "next.percent = top_spot_battery_percent(next.voltage_mv);",
            "next.level = top_spot_battery_level(next.percent);",
            "s_status = next;",
        ]
        positions = [worker.index(line) for line in assignments]
        self.assertEqual(positions, sorted(positions))
        ui = (ROOT / "main" / "ui_screens.c").read_text()
        updater = ui.split("static void update_battery_status_label(void)", 1)[1].split(
            "static void battery_status_timer_cb", 1)[0]
        self.assertEqual(updater.count("top_spot_battery_get_status(&battery);"), 1)
        self.assertIn("int voltage_cv = (battery.voltage_mv + 5) / 10;", updater)
        self.assertIn("icon, battery.percent, voltage_cv / 100, voltage_cv % 100, state", updater)
        self.assertNotIn("top_spot_battery_percent(", updater)
        self.assertNotIn("top_spot_battery_correct_voltage_mv(", updater)
        self.assertNotIn("adc_", updater)

    def test_interpolation(self):
        for mv, percent in [(3400, 3), (3650, 15), (3775, 35), (4050, 85)]:
            self.assertEqual(self.lib.top_spot_battery_percent(mv), percent)

    def test_clamp_and_monotonicity(self):
        values = [self.lib.top_spot_battery_percent(mv) for mv in range(-100, 5001)]
        self.assertEqual(values[0], 0)
        self.assertEqual(values[-1], 100)
        self.assertTrue(all(0 <= p <= 100 for p in values))
        self.assertTrue(all(a <= b for a, b in zip(values, values[1:])))

    def test_warning_boundaries(self):
        for percent, level in [(0, 3), (5, 3), (6, 2), (10, 2), (11, 1),
                               (20, 1), (21, 0), (100, 0)]:
            self.assertEqual(self.lib.top_spot_battery_level(percent), level)

    def test_filter_start_and_step(self):
        state = Filter()
        sample = self.lib.top_spot_battery_filter_mv
        self.assertEqual(sample(ctypes.byref(state), 1200), 1200)
        self.assertEqual(sample(ctypes.byref(state), 1400), 1250)
        values = [sample(ctypes.byref(state), 1400) for _ in range(40)]
        self.assertTrue(all(1250 <= v <= 1400 for v in values))
        self.assertEqual(values[-1], 1400)
        values = [sample(ctypes.byref(state), 1200) for _ in range(40)]
        self.assertTrue(all(1200 <= v <= 1400 for v in values))
        self.assertEqual(values[-1], 1200)

    def test_filter_reduces_noise(self):
        state = Filter()
        sample = self.lib.top_spot_battery_filter_mv
        sample(ctypes.byref(state), 1300)
        values = [sample(ctypes.byref(state), 1290 if i % 2 else 1310)
                  for i in range(100)]
        self.assertLessEqual(max(values) - min(values), 5)

    def test_filter_fractional_convergence_and_restart(self):
        state = Filter()
        sample = self.lib.top_spot_battery_filter_mv
        sample(ctypes.byref(state), 1200)
        for _ in range(40):
            result = sample(ctypes.byref(state), 1201)
        self.assertEqual(result, 1201)
        state.initialized = False
        self.assertEqual(sample(ctypes.byref(state), 1100), 1100)


if __name__ == "__main__":
    unittest.main(verbosity=2)
