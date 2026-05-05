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
module test.peak_logic

state spectrum[3] = 0
state peaks[3] = 0
state last_decay_time = 0
state current_time = 0
state time_diff = 0

loop {
  // Simulate the actual script logic
  for x in spectrum {
    peaks[x] = max(peaks[x], spectrum[x])
  }

  current_time = millis()
  time_diff = current_time - last_decay_time

  if time_diff >= 90 {
    last_decay_time = current_time
    for x in spectrum {
      peaks[x] = max(peaks[x] - 1, spectrum[x])
    }
  }
}
)";

int main() {
    printf("=== Peak Logic Test (with spectrum update) ===\n\n");

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

    printf("✓ Compilation successful\n\n");

    // Create VM
    bottle_vm_t* vm = (bottle_vm_t*)malloc(sizeof(bottle_vm_t));
    bottle_vm_init(vm, program);

    // Initialize: spectrum[0]=5, spectrum[1]=7, spectrum[2]=7
    // This simulates high spectrum values that would prevent decay
    vm->arrays[0][0] = 5;
    vm->arrays[0][1] = 7;
    vm->arrays[0][2] = 7;

    // Initialize peaks to 10
    vm->arrays[1][0] = 10;
    vm->arrays[1][1] = 10;
    vm->arrays[1][2] = 10;

    printf("Initial state:\n");
    printf("  spectrum = [%d, %d, %d]\n", vm->arrays[0][0], vm->arrays[0][1], vm->arrays[0][2]);
    printf("  peaks    = [%d, %d, %d]\n\n", vm->arrays[1][0], vm->arrays[1][1], vm->arrays[1][2]);

    // Create module context
    module_context_t ctx = {};

    // Frame 1: t=0ms (no decay yet)
    ctx.now_ms = 0;
    printf("Frame 1 (t=0ms):\n");
    bottle_vm_execute(vm, program, program->loop_offset, &ctx);
    if (vm->error.has_error) {
        printf("  ERROR: ");
        bottle_error_print(&vm->error, "Runtime");
        return 1;
    }
    printf("  peaks = [%d, %d, %d]\n\n", vm->arrays[1][0], vm->arrays[1][1], vm->arrays[1][2]);

    // Frame 2: t=100ms (decay should happen)
    ctx.now_ms = 100;
    printf("Frame 2 (t=100ms, decay expected):\n");
    bottle_vm_execute(vm, program, program->loop_offset, &ctx);
    if (vm->error.has_error) {
        printf("  ERROR: ");
        bottle_error_print(&vm->error, "Runtime");
        return 1;
    }
    printf("  peaks = [%d, %d, %d]\n", vm->arrays[1][0], vm->arrays[1][1], vm->arrays[1][2]);
    printf("  Expected: [9, 9, 9] if decay works\n");
    printf("  Expected: [10, 10, 10] if spectrum blocks decay\n\n");

    // Now lower spectrum values to allow decay
    vm->arrays[0][0] = 3;
    vm->arrays[0][1] = 3;
    vm->arrays[0][2] = 3;
    printf("Lowering spectrum to [3, 3, 3]\n\n");

    // Frame 3: t=200ms (decay should work now)
    ctx.now_ms = 200;
    printf("Frame 3 (t=200ms, decay expected):\n");
    bottle_vm_execute(vm, program, program->loop_offset, &ctx);
    if (vm->error.has_error) {
        printf("  ERROR: ");
        bottle_error_print(&vm->error, "Runtime");
        return 1;
    }
    printf("  peaks = [%d, %d, %d]\n", vm->arrays[1][0], vm->arrays[1][1], vm->arrays[1][2]);
    printf("  Expected: [8, 8, 8] if decay works when spectrum is low\n\n");

    free(vm);
    free(program);

    printf("This test demonstrates that high spectrum values prevent peak decay.\n");
    return 0;
}
