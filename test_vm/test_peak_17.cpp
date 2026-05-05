#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdarg.h>

// Include mock headers first
#include "Arduino.h"
#include "common.h"
#include "module_runtime.h"

// Define the Serial object
MockSerial Serial;

// Include the actual source files
#include "../src/bottle_error.cpp"
#include "../src/bottle_compiler.cpp"
#include "bottle_builtins_mock.cpp"
#include "../src/bottle_vm.cpp"

const char* test_script = R"(
runtime bottle-vm@0.2
module test.peak_17

state spectrum[17] = 0
state peaks[17] = 0
state last_decay_time = 0
state current_time = 0
state time_diff = 0

loop {
  current_time = millis()
  time_diff = current_time - last_decay_time

  if time_diff >= 90 {
    last_decay_time = current_time
    for x in spectrum {
      peaks[x] = max(peaks[x] - 1, 0)
    }
  }
}
)";

int main() {
    printf("=== Peak Decay Test (17 elements) ===\n\n");

    bottle_program_t* program = (bottle_program_t*)malloc(sizeof(bottle_program_t));
    memset(program, 0, sizeof(*program));

    printf("Compiling test script...\n");
    bool result = bottle_compile(test_script, program);

    if (!result) {
        printf("FAILED: Compilation error\n");
        if (program->error.has_error) {
            bottle_error_print(&program->error, "Compile");
        }
        return 1;
    }

    printf("✓ Compilation successful\n");
    printf("  Scalars: %d\n", program->scalar_count);
    printf("  Arrays: %d\n\n", program->array_count);

    // Create VM
    bottle_vm_t* vm = (bottle_vm_t*)malloc(sizeof(bottle_vm_t));
    bottle_vm_init(vm, program);

    // Initialize all peaks to 10
    for (int i = 0; i < 17; i++) {
        vm->arrays[1][i] = 10;
    }

    printf("Initial state: all peaks = 10\n\n");

    // Create module context
    module_context_t ctx = {};

    // Frame 1: t=0ms
    ctx.now_ms = 0;
    printf("Frame 1 (t=0ms):\n");
    bottle_vm_execute(vm, program, program->loop_offset, &ctx);
    if (vm->error.has_error) {
        printf("  ERROR: ");
        bottle_error_print(&vm->error, "Runtime");
        return 1;
    }
    printf("  peaks[0]=%d, peaks[1]=%d, peaks[8]=%d, peaks[16]=%d\n\n",
           vm->arrays[1][0], vm->arrays[1][1], vm->arrays[1][8], vm->arrays[1][16]);

    // Frame 2: t=100ms (should decay)
    ctx.now_ms = 100;
    printf("Frame 2 (t=100ms, decay expected):\n");
    bottle_vm_execute(vm, program, program->loop_offset, &ctx);
    if (vm->error.has_error) {
        printf("  ERROR: ");
        bottle_error_print(&vm->error, "Runtime");
        return 1;
    }
    printf("  peaks[0]=%d, peaks[1]=%d, peaks[8]=%d, peaks[16]=%d\n\n",
           vm->arrays[1][0], vm->arrays[1][1], vm->arrays[1][8], vm->arrays[1][16]);

    // Frame 3: t=200ms (should decay again)
    ctx.now_ms = 200;
    printf("Frame 3 (t=200ms, decay expected):\n");
    bottle_vm_execute(vm, program, program->loop_offset, &ctx);
    if (vm->error.has_error) {
        printf("  ERROR: ");
        bottle_error_print(&vm->error, "Runtime");
        return 1;
    }
    printf("  peaks[0]=%d, peaks[1]=%d, peaks[8]=%d, peaks[16]=%d\n\n",
           vm->arrays[1][0], vm->arrays[1][1], vm->arrays[1][8], vm->arrays[1][16]);

    // Check all peaks
    printf("All peaks after 2 decay cycles:\n");
    bool all_same = true;
    int first_val = vm->arrays[1][0];
    for (int i = 0; i < 17; i++) {
        printf("  peaks[%2d] = %d\n", i, vm->arrays[1][i]);
        if (vm->arrays[1][i] != first_val) {
            all_same = false;
        }
    }
    printf("\n");

    free(vm);
    free(program);

    if (all_same && first_val == 8) {
        printf("✓ TEST PASSED: All 17 peaks decayed equally (10 → 9 → 8)\n");
        return 0;
    } else {
        printf("✗ TEST FAILED: Peaks did not decay equally\n");
        printf("  Expected: all = 8, Actual: first = %d, all_same = %s\n",
               first_val, all_same ? "YES" : "NO");
        return 1;
    }
}
