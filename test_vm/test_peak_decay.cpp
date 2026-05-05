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
module test.peak_decay

state spectrum[3] = 0
state peaks[3] = 0
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
    printf("=== Peak Decay Test ===\n\n");

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
    printf("  Arrays: %d\n", program->array_count);
    printf("\n");

    // Print scalar names
    printf("Scalar definitions:\n");
    for (int i = 0; i < program->scalar_count; i++) {
        printf("  scalar[%d]: %s = %.1f\n", i, program->scalars[i].name, program->scalars[i].initial_value);
    }
    printf("\n");

    // Create VM
    bottle_vm_t* vm = (bottle_vm_t*)malloc(sizeof(bottle_vm_t));
    bottle_vm_init(vm, program);

    // Initialize peaks array to [10, 10, 10]
    vm->arrays[1][0] = 10;
    vm->arrays[1][1] = 10;
    vm->arrays[1][2] = 10;

    printf("Initial state:\n");
    printf("  peaks = [%d, %d, %d]\n\n", vm->arrays[1][0], vm->arrays[1][1], vm->arrays[1][2]);

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
    printf("  peaks = [%d, %d, %d]\n", vm->arrays[1][0], vm->arrays[1][1], vm->arrays[1][2]);
    printf("  last_decay_time = %.0f\n", vm->scalars[2]);
    printf("  stack_top = %d\n\n", vm->stack_top);

    // Frame 2: t=50ms (should NOT decay, < 90ms)
    ctx.now_ms = 50;
    printf("Frame 2 (t=50ms, no decay expected):\n");
    bottle_vm_execute(vm, program, program->loop_offset, &ctx);
    if (vm->error.has_error) {
        printf("  ERROR: ");
        bottle_error_print(&vm->error, "Runtime");
        return 1;
    }
    printf("  peaks = [%d, %d, %d]\n", vm->arrays[1][0], vm->arrays[1][1], vm->arrays[1][2]);
    printf("  last_decay_time = %.0f\n", vm->scalars[2]);
    printf("  stack_top = %d\n\n", vm->stack_top);

    // Frame 3: t=100ms (should decay by 1)
    ctx.now_ms = 100;
    printf("Frame 3 (t=100ms, decay expected):\n");
    bottle_vm_execute(vm, program, program->loop_offset, &ctx);
    if (vm->error.has_error) {
        printf("  ERROR: ");
        bottle_error_print(&vm->error, "Runtime");
        return 1;
    }
    printf("  peaks = [%d, %d, %d]\n", vm->arrays[1][0], vm->arrays[1][1], vm->arrays[1][2]);
    printf("  last_decay_time = %.0f\n", vm->scalars[2]);
    printf("  stack_top = %d\n\n", vm->stack_top);

    // Frame 4: t=200ms (should decay again)
    ctx.now_ms = 200;
    printf("Frame 4 (t=200ms, decay expected):\n");
    bottle_vm_execute(vm, program, program->loop_offset, &ctx);
    if (vm->error.has_error) {
        printf("  ERROR: ");
        bottle_error_print(&vm->error, "Runtime");
        return 1;
    }
    printf("  peaks = [%d, %d, %d]\n", vm->arrays[1][0], vm->arrays[1][1], vm->arrays[1][2]);
    printf("  last_decay_time = %.0f\n", vm->scalars[2]);
    printf("  stack_top = %d\n\n", vm->stack_top);

    // Verify results
    bool all_same = (vm->arrays[1][0] == vm->arrays[1][1]) && (vm->arrays[1][1] == vm->arrays[1][2]);
    bool all_decayed = (vm->arrays[1][0] < 10);

    printf("Results:\n");
    printf("  All peaks same value: %s\n", all_same ? "YES" : "NO");
    printf("  All peaks decayed: %s\n", all_decayed ? "YES" : "NO");
    printf("  Final values: [%d, %d, %d]\n\n", vm->arrays[1][0], vm->arrays[1][1], vm->arrays[1][2]);

    free(vm);
    free(program);

    if (all_same && all_decayed) {
        printf("✓ TEST PASSED: All peaks decayed equally\n");
        return 0;
    } else {
        printf("✗ TEST FAILED: Peaks did not decay correctly\n");
        return 1;
    }
}
