#pragma once
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Arduino macros
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif

// Arduino functions
inline long constrain(long x, long a, long b) {
    if (x < a) return a;
    if (x > b) return b;
    return x;
}

inline long random(long min_val, long max_val) {
    return min_val + (rand() % (max_val - min_val));
}

inline long random(long max_val) {
    return rand() % max_val;
}

inline unsigned long millis() {
    return (unsigned long)(clock() * 1000 / CLOCKS_PER_SEC);
}

// Mock Arduino.h for native testing
class MockSerial {
public:
    void printf(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
    }
    void println(const char* str) {
        ::printf("%s\n", str);
    }
};

extern MockSerial Serial;
