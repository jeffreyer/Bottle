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
module test.if_for

state values[5] = 10
state should_update = 1

loop {
  if should_update >= 1 {
    for x in values {
      values[x] = values[x] - 1
    }
  }
}
)";

int main() {
    printf("=== If-For Nested Test ===\n\n");

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

    printf("Initial values: [%d, %d, %d, %d, %d]\n\n",
           vm->arrays[0][0], vm->arrays[0][1], vm->arrays[0][2],
           vm->arrays[0][3], vm->arrays[0][4]);

    // Create module context
    module_context_t ctx = {};
    ctx.now_ms = 0;

    // Frame 1
    printf("Frame 1:\n");
    bottle_vm_execute(vm, program, program->loop_offset, &ctx);
    if (vm->error.has_error) {
        printf("  ERROR: ");
        bottle_error_print(&vm->error, "Runtime");
        return 1;
    }
    printf("  values: [%d, %d, %d, %d, %d]\n",
           vm->arrays[0][0], vm->arrays[0][1], vm->arrays[0][2],
           vm->arrays[0][3], vm->arrays[0][4]);
    printf("  stack_top: %d\n\n", vm->stack_top);

    // Frame 2
    printf("Frame 2:\n");
    bottle_vm_execute(vm, program, program->loop_offset, &ctx);
    if (vm->error.has_error) {
        printf("  ERROR: ");
        bottle_error_print(&vm->error, "Runtime");
        return 1;
    }
    printf("  values: [%d, %d, %d, %d, %d]\n",
           vm->arrays[0][0], vm->arrays[0][1], vm->arrays[0][2],
           vm->arrays[0][3], vm->arrays[0][4]);
    printf("  stack_top: %d\n\n", vm->stack_top);

    // Check results before freeing
    bool all_same = (vm->arrays[0][0] == vm->arrays[0][1]) &&
                    (vm->arrays[0][1] == vm->arrays[0][2]) &&
                    (vm->arrays[0][2] == vm->arrays[0][3]) &&
                    (vm->arrays[0][3] == vm->arrays[0][4]);
    int final_value = vm->arrays[0][0];

    free(vm);
    free(program);

    if (all_same && final_value == 8) {
        printf("✓ TEST PASSED: All values decremented equally (10 → 9 → 8)\n");
        return 0;
    } else {
        printf("✗ TEST FAILED: Values did not decrement equally\n");
        printf("  Expected: all = 8, Actual: final_value = %d, all_same = %s\n",
               final_value, all_same ? "YES" : "NO");
        return 1;
    }
}
