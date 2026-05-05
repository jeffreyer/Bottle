#include <stdio.h>
#include <string.h>

// Minimal stubs for ESP32 dependencies
namespace std {
    void __throw_bad_alloc() { printf("Bad alloc!\n"); }
}

extern "C" {
    void Serial_printf(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
    }
}

// Mock Serial class
class MockSerial {
public:
    void printf(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
    }
    void println(const char* str) {
        printf("%s\n", str);
    }
} Serial;

// Include compiler header
extern "C" {
    #include "bottle_compiler.h"
}

// Declare the compile function
extern bool bottle_compile(const char* source, void* program);

int main() {
    const char* test_script = R"(
runtime bottle-vm@0.2
module test

state x = 0
state arr[16] = 0

setup {
    x = 10
}

loop {
    for i in range(0, 16) {
        arr[i] = i * 2
    }
}
)";

    printf("=== Bottle VM Native Test ===\n\n");
    printf("Testing script:\n%s\n", test_script);
    printf("\n=== Compilation Output ===\n");

    // Try to compile (program structure doesn't matter for now, just testing tokenizer/parser)
    char dummy_program[1024] = {0};
    bool result = bottle_compile(test_script, dummy_program);

    printf("\n=== Result ===\n");
    printf("Compilation %s\n", result ? "SUCCEEDED" : "FAILED");

    return result ? 0 : 1;
}
