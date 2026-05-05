#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "Arduino.h"
#include "bottle_compiler.h"
#include "bottle_vm.h"
#include "bottle_error.h"

// Define Serial object
MockSerial Serial;

// Mock module context
module_context_t g_ctx = {};

const char* test_script = R"(
runtime bottle-vm@0.2
module test.peak_decay

state peaks[3] = 10
state last_decay_time = 0
state current_time = 0
state time_diff = 0

frame_ms 33

loop {
  current_time = millis()
  time_diff = current_time - last_decay_time

  if time_diff >= 90 {
    last_decay_time = current_time
    for x in peaks {
      peaks[x] = max(peaks[x] - 1, 0)
    }
  }
}
)";

int main() {
    printf("=== Testing Peak Decay Logic ===\n\n");

    bottle_program_t* program = (bottle_program_t*)malloc(sizeof(bottle_program_t));
    if (!bottle_compile(test_script, program)) {
        printf("FAILED: Compilation error\n");
        if (program->error.has_error) {
            bottle_error_print(&program->error, "Compile");
        }
        return 1;
    }

    printf("Compilation successful!\n");
    printf("Scalars: %d\n", program->scalar_count);
    for (int i = 0; i < program->scalar_count; i++) {
        printf("  scalar[%d]: %s = %.1f\n", i, program->scalars[i].name, program->scalars[i].initial_value);
    }
    printf("\n");

    bottle_vm_t* vm = (bottle_vm_t*)malloc(sizeof(bottle_vm_t));
    bottle_vm_init(vm, program);

    // Initialize peaks array to [10, 10, 10]
    vm->arrays[0][0] = 10;
    vm->arrays[0][1] = 10;
    vm->arrays[0][2] = 10;

    printf("Initial peaks: [%d, %d, %d]\n\n", vm->arrays[0][0], vm->arrays[0][1], vm->arrays[0][2]);

    // Run multiple frames
    for (int frame = 0; frame < 5; frame++) {
        g_ctx.now_ms = frame * 100;  // 100ms per frame
        printf("Frame %d (t=%d ms):\n", frame, g_ctx.now_ms);

        bottle_vm_execute(vm, program, program->loop_offset, &g_ctx);

        if (vm->error.has_error) {
            printf("  ERROR: ");
            bottle_error_print(&vm->error, "Runtime");
            break;
        }

        printf("  peaks: [%d, %d, %d]\n", vm->arrays[0][0], vm->arrays[0][1], vm->arrays[0][2]);
        printf("  last_decay_time: %.0f\n", vm->scalars[1]);
        printf("  stack_top: %d\n\n", vm->stack_top);
    }

    // Check results
    bool all_decayed = (vm->arrays[0][0] < 10) && (vm->arrays[0][1] < 10) && (vm->arrays[0][2] < 10);
    bool all_same = (vm->arrays[0][0] == vm->arrays[0][1]) && (vm->arrays[0][1] == vm->arrays[0][2]);

    printf("Results:\n");
    printf("  All peaks decayed: %s\n", all_decayed ? "YES" : "NO");
    printf("  All peaks same value: %s\n", all_same ? "YES" : "NO");

    free(vm);
    free(program);

    if (all_decayed && all_same) {
        printf("\n✓ TEST PASSED\n");
        return 0;
    } else {
        printf("\n✗ TEST FAILED\n");
        return 1;
    }
}
