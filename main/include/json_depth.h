#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Pre-parse guard: false if [ / { nesting (outside strings) exceeds max or
 * goes negative. cJSON recurses per level, so this keeps it off a small stack. */
static inline bool json_depth_ok(const char *s, size_t n, int max)
{
    int depth = 0;
    bool in_str = false;

    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (in_str) {
            if (c == '\\') {
                i++;
            } else if (c == '"') {
                in_str = false;
            }
        } else if (c == '"') {
            in_str = true;
        } else if (c == '[' || c == '{') {
            if (++depth > max) {
                return false;
            }
        } else if ((c == ']' || c == '}') && --depth < 0) {
            return false;
        }
    }
    return true;
}
