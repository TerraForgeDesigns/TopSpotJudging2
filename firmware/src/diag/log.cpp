#include "log.h"

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace diag {

namespace {

constexpr int MAX_LINES = 40;
constexpr int MAX_LINE_LEN = 96;  // including the null terminator

char g_lines[MAX_LINES][MAX_LINE_LEN];
int g_count = 0;      // how many of g_lines are actually populated (<= MAX_LINES)
int g_nextSlot = 0;   // ring cursor — the next slot log() writes into

}  // namespace

void log(const char* fmt, ...) {
    char buf[MAX_LINE_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    Serial.println(buf);

    strncpy(g_lines[g_nextSlot], buf, MAX_LINE_LEN - 1);
    g_lines[g_nextSlot][MAX_LINE_LEN - 1] = '\0';
    g_nextSlot = (g_nextSlot + 1) % MAX_LINES;
    if (g_count < MAX_LINES) g_count++;
}

void snapshot(char* out, unsigned int outSize) {
    if (outSize == 0) return;
    out[0] = '\0';
    size_t used = 0;
    // Oldest first: when the ring hasn't wrapped yet, that's just slot 0;
    // once it has, the oldest surviving line is the one about to be
    // overwritten next (g_nextSlot).
    int start = (g_count < MAX_LINES) ? 0 : g_nextSlot;
    for (int i = 0; i < g_count; i++) {
        int idx = (start + i) % MAX_LINES;
        size_t lineLen = strlen(g_lines[idx]);
        // +1 for the newline this line adds, +1 for the terminator the
        // buffer must always keep room for.
        if (used + lineLen + 2 >= outSize) break;
        memcpy(out + used, g_lines[idx], lineLen);
        used += lineLen;
        out[used++] = '\n';
        out[used] = '\0';
    }
}

}  // namespace diag
