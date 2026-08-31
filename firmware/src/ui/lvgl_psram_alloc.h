// LV_MEM_CUSTOM hooks (see include/lv_conf.h) — routes every LVGL
// object/style/animation allocation to PSRAM via ESP-IDF's heap_caps
// allocator, instead of LVGL's own fixed-size internal pool (which would
// otherwise compete with FreeRTOS task stacks, camera/SD DMA buffers, and
// the WiFi stack for the ESP32-S3's ~512KB internal SRAM).
//
// Included from lv_mem.c, which LVGL compiles as plain C — every symbol
// here must have C linkage, and this header must not pull in any C++-only
// construct (no Arduino.h, no classes, no namespaces).
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* lvgl_psram_malloc(size_t size);
void lvgl_psram_free(void* ptr);
void* lvgl_psram_realloc(void* ptr, size_t new_size);

#ifdef __cplusplus
}
#endif
