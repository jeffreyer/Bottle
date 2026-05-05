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
module test.multiple_for

state arr1[3] = 0
state arr2[3] = 0
state arr3[3] = 0
state arr4[3] = 0

loop {
  // Four for loops using same variable name 'x'
  for x in arr1 {
    arr1[x] = x + 1
  }

  for x in arr2 {
    arr2[x] = x + 10
  }

  for x in arr3 {
    arr3[x] = x + 20
  }

  for x in arr4 {
    arr4[x] = x + 30
  }
}
)";

int main() {
    printf("=== Multiple For Loops Test ===\n\n");

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

    // Print scalar names
    printf("Scalar definitions:\n");
    for (int i = 0; i < program->scalar_count; i++) {
        printf("  scalar[%d]: %s\n", i, program->scalars[i].name);
    }
    printf("\n");

    // Create VM
    bottle_vm_t* vm = (bottle_vm_t*)malloc(sizeof(bottle_vm_t));
    bottle_vm_init(vm, program);

    printf("Initial state: all arrays = [0, 0, 0]\n\n");

    // Create module context
    module_context_t ctx = {};
    ctx.now_ms = 0;

    printf("Executing loop...\n");
    bottle_vm_execute(vm, program, program->loop_offset, &ctx);
    if (vm->error.has_error) {
        printf("  ERROR: ");
        bottle_error_print(&vm->error, "Runtime");
        return 1;
    }

    printf("\nResults:\n");
    printf("  arr1 = [%d, %d, %d] (expected [1, 2, 3])\n",
           vm->arrays[0][0], vm->arrays[0][1], vm->arrays[0][2]);
    printf("  arr2 = [%d, %d, %d] (expected [10, 11, 12])\n",
           vm->arrays[1][0], vm->arrays[1][1], vm->arrays[1][2]);
    printf("  arr3 = [%d, %d, %d] (expected [20, 21, 22])\n",
           vm->arrays[2][0], vm->arrays[2][1], vm->arrays[2][2]);
    printf("  arr4 = [%d, %d, %d] (expected [30, 31, 32])\n",
           vm->arrays[3][0], vm->arrays[3][1], vm->arrays[3][2]);
    printf("  stack_top = %d\n\n", vm->stack_top);

    bool arr1_ok = (vm->arrays[0][0] == 1 && vm->arrays[0][1] == 2 && vm->arrays[0][2] == 3);
    bool arr2_ok = (vm->arrays[1][0] == 10 && vm->arrays[1][1] == 11 && vm->arrays[1][2] == 12);
    bool arr3_ok = (vm->arrays[2][0] == 20 && vm->arrays[2][1] == 21 && vm->arrays[2][2] == 22);
    bool arr4_ok = (vm->arrays[3][0] == 30 && vm->arrays[3][1] == 31 && vm->arrays[3][2] == 32);

    free(vm);
    free(program);

    if (arr1_ok && arr2_ok && arr3_ok && arr4_ok) {
        printf("✓ TEST PASSED: All four for loops executed correctly\n");
        return 0;
    } else {
        printf("✗ TEST FAILED: Some for loops did not execute correctly\n");
        return 1;
    }
}
