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

bool compile_and_run(const char* script, bottle_program_t** out_program, bottle_vm_t** out_vm, module_context_t* ctx) {
    *out_program = (bottle_program_t*)malloc(sizeof(bottle_program_t));
    if (!bottle_compile(script, *out_program)) {
        return false;
    }

    *out_vm = (bottle_vm_t*)malloc(sizeof(bottle_vm_t));
    bottle_vm_init(*out_vm, *out_program);

    if ((*out_program)->has_loop) {
        bottle_vm_execute(*out_vm, *out_program, (*out_program)->loop_offset, ctx);
    }

    return !(*out_vm)->error.has_error;
}

void cleanup(bottle_program_t* program, bottle_vm_t* vm) {
    if (program) free(program);
    if (vm) free(vm);
}

int main() {
    printf("=== Testing for-loop with array[17] ===\n\n");

    const char* script = R"(
        runtime bottle-vm@0.2
        module test.for_array
        state arr[17] = 0
        state last_x = 0
        loop {
            for x in arr {
                last_x = x
            }
        }
    )";

    module_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    bottle_program_t* program = NULL;
    bottle_vm_t* vm = NULL;

    if (!compile_and_run(script, &program, &vm, &ctx)) {
        printf("FAILED: Compilation or execution error\n");
        if (vm && vm->error.has_error) {
            bottle_error_print(&vm->error, "Runtime");
        }
        return 1;
    }

    printf("After loop execution:\n");
    printf("  last_x = %.1f\n", vm->scalars[0]);
    printf("  Expected: 16.0 (last valid index 0-16)\n");
    printf("  Stack top: %d (should be 0)\n", vm->stack_top);

    bool passed = (vm->scalars[0] == 16.0f && vm->stack_top == 0);

    cleanup(program, vm);

    if (passed) {
        printf("\n✓ TEST PASSED\n");
        return 0;
    } else {
        printf("\n✗ TEST FAILED\n");
        return 1;
    }
}
