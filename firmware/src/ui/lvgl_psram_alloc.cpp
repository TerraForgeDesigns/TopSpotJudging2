#include "lvgl_psram_alloc.h"

#include <esp_heap_caps.h>

extern "C" {

void* lvgl_psram_malloc(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM); }

void lvgl_psram_free(void* ptr) { heap_caps_free(ptr); }

void* lvgl_psram_realloc(void* ptr, size_t new_size) {
    return heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);
}

}  // extern "C"
