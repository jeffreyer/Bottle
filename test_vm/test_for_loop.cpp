#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

// Mock Arduino Serial
struct {
    void printf(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
    }
} Serial;

// Mock module context
typedef struct {
    uint32_t now_ms;
} module_context_t;

module_context_t g_ctx = {};

#include "../src/bottle_types.cpp"
#include "../src/bottle_error.cpp"
#include "../src/bottle_compiler.cpp"
#include "../src/bottle_vm.cpp"

int main() {
    printf("=== Testing for-loop bug ===\n\n");

    const char* script = R"(
runtime bottle-vm@0.2
module test.forloop

state arr[17] = 0
state x_var = 0

frame_ms 33

loop {
  for x in arr {
    x_var = x
  }
}
)";

    printf("Compiling script...\n");
    bottle_program_t* program = (bottle_program_t*)malloc(sizeof(bottle_program_t));
    if (!bottle_compile(script, program)) {
        printf("Compilation failed!\n");
        if (program->error.has_error) {
            bottle_error_print(&program->error, "Compile");
        }
        return 1;
    }

    printf("Compilation successful!\n");
    printf("Bytecode size: %d\n", program->bytecode_size);
    printf("Scalar count: %d\n", program->scalar_count);
    printf("Array count: %d\n\n", program->array_count);

    // Initialize VM
    bottle_vm_t* vm = (bottle_vm_t*)malloc(sizeof(bottle_vm_t));
    bottle_vm_init(vm, program);

    printf("Running loop section...\n");
    g_ctx.now_ms = 0;
    bottle_vm_execute(vm, program, program->loop_offset, &g_ctx);

    if (vm->error.has_error) {
        printf("\nVM Error:\n");
        bottle_error_print(&vm->error, "Runtime");
        free(vm);
        free(program);
        return 1;
    }

    printf("\nAfter loop execution:\n");
    printf("x_var (scalar[1]) = %.1f\n", vm->scalars[1]);
    printf("Expected: 16.0 (last valid index)\n");
    printf("Stack top: %d (should be 0)\n", vm->stack_top);

    if (vm->scalars[1] == 16.0f && vm->stack_top == 0) {
        printf("\n✓ TEST PASSED\n");
        free(vm);
        free(program);
        return 0;
    } else {
        printf("\n✗ TEST FAILED\n");
        free(vm);
        free(program);
        return 1;
    }
}
