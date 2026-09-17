# Top Spot Waveshare P4 Bring-Up

Clean ESP-IDF hardware bring-up target for the Waveshare ESP32-P4-WIFI6-Touch-LCD-7B.

This target is intentionally isolated from the existing Elecrow/ESP32-S3 PlatformIO firmware in `../firmware`.
It only initializes the Waveshare EK79007 1024 x 600 MIPI-DSI display, GT911 touch, LVGL 9, and a simple Top Spot hardware test screen.

Authoritative hardware references:

- `C:\Users\jrod0\Desktop\Waveshare-P4-Reference\examples\esp-idf\08_lvgl_display_panel`
- `C:\Users\jrod0\Desktop\Waveshare-P4-Reference\examples\esp-idf\09_lvgl_demo_v9`
- `C:\Users\jrod0\Desktop\Waveshare-P4-Reference\config\esp32p4_rev1_3.defaults`

Build:

```powershell
cd firmware_p4
idf.py set-target esp32p4
idf.py build
```

Do not flash from this milestone unless hardware bring-up testing is explicitly requested.
