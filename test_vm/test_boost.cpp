#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Arduino.h"
#include "bottle_compiler.h"
#include "bottle_vm.h"
#include "bottle_error.h"
#include "module_runtime.h"

// Define Serial instance
MockSerial Serial;

// Mock context
module_context_t mock_ctx;

const char* test_script = R"(runtime bottle-vm@0.2
module test.boost

state values[5] = 100
state multipliers[5] = [0.5, 1.0, 1.5, 2.0, 2.5]
state results[5] = 0

frame_ms 100

setup {
  clear(LEDS)
}

loop {
  for x in values {
    print(values[x])
    print(multipliers[x])
    results[x] = values[x] * multipliers[x]
    print(results[x])
  }
}

unload {
  clear(LEDS)
  show(LEDS)
}
)";

int main() {
    printf("=== Testing Float Array * Int Array ===\n\n");

    bottle_program_t* program = (bottle_program_t*)malloc(sizeof(bottle_program_t));
    if (!program) {
        printf("Failed to allocate program\n");
        return 1;
    }

    bool success = bottle_compile(test_script, program);
    if (!success || program->error.has_error) {
        printf("Compilation error at line %d, col %d: %s\n",
               program->error.line, program->error.column, program->error.message);
        free(program);
        return 1;
    }

    printf("Compilation successful!\n");
    printf("Bytecode size: %d bytes\n", program->bytecode_size);
    printf("Constants: %d\n", program->constant_count);
    printf("Arrays: %d\n\n", program->array_count);

    // Print array info
    for (int i = 0; i < program->array_count; i++) {
        printf("Array %d (%s): length=%d, type=%s\n",
               i,
               program->arrays[i].name,
               program->arrays[i].length,
               program->arrays[i].element_type == BOTTLE_TYPE_INT ? "int" : "float");
    }
    printf("\n");

    // Print constants
    printf("Constants:\n");
    for (int i = 0; i < program->constant_count; i++) {
        printf("  [%d] ", i);
        switch (program->constants[i].type) {
            case BOTTLE_TYPE_INT:
                printf("int: %d\n", program->constants[i].as.i);
                break;
            case BOTTLE_TYPE_FLOAT:
                printf("float: %.3f\n", program->constants[i].as.f);
                break;
            case BOTTLE_TYPE_BOOL:
                printf("bool: %s\n", program->constants[i].as.b ? "true" : "false");
                break;
            default:
                printf("unknown type\n");
                break;
        }
    }
    printf("\n");

    // Disassemble bytecode
    printf("Bytecode disassembly:\n");
    uint16_t pc = 0;
    while (pc < program->bytecode_size) {
        uint8_t op = program->bytecode[pc];
        printf("  %04d: %s", pc, bottle_opcode_name((bottle_opcode_t)op));
        pc++;

        // Print arguments based on opcode
        switch ((bottle_opcode_t)op) {
            case OP_PUSH_CONST:
            case OP_JUMP:
            case OP_JUMP_IF_FALSE:
            case OP_JUMP_IF_TRUE: {
                uint16_t arg = (program->bytecode[pc] << 8) | program->bytecode[pc+1];
                printf(" %d", arg);
                pc += 2;
                break;
            }
            case OP_PUSH_SCALAR:
            case OP_PUSH_ARRAY:
            case OP_POP_SCALAR:
            case OP_POP_ARRAY:
            case OP_READ_SPECTRUM: {
                printf(" %d", program->bytecode[pc]);
                pc++;
                break;
            }
            case OP_INIT_ARRAY_LITERAL: {
                uint8_t array_idx = program->bytecode[pc++];
                uint8_t count = program->bytecode[pc++];
                printf(" array=%d count=%d [", array_idx, count);
                for (uint8_t i = 0; i < count; i++) {
                    uint16_t const_idx = (program->bytecode[pc] << 8) | program->bytecode[pc+1];
                    printf("%d", const_idx);
                    if (i < count - 1) printf(", ");
                    pc += 2;
                }
                printf("]");
                break;
            }
            default:
                break;
        }
        printf("\n");

        if (op == OP_HALT) break;
    }
    printf("\n");

    // Initialize VM
    bottle_vm_t* vm = (bottle_vm_t*)malloc(sizeof(bottle_vm_t));
    if (!vm) {
        printf("Failed to allocate VM\n");
        free(program);
        return 1;
    }

    bottle_vm_init(vm, program);

    printf("Running loop phase (1 iteration)...\n");
    printf("Loop offset: %d\n", program->loop_offset);
    printf("Expected output:\n");
    printf("  50   (100 * 0.5)\n");
    printf("  100  (100 * 1.0)\n");
    printf("  150  (100 * 1.5)\n");
    printf("  200  (100 * 2.0)\n");
    printf("  250  (100 * 2.5)\n");
    printf("\nActual output:\n");

    // Initialize mock context
    memset(&mock_ctx, 0, sizeof(mock_ctx));
    mock_ctx.now_ms = 0;

    bottle_vm_execute(vm, program, program->loop_offset, &mock_ctx);

    if (vm->error.has_error) {
        printf("\nRuntime error at line %d: %s\n",
               vm->error.line, vm->error.message);
        free(vm);
        free(program);
        return 1;
    }

    printf("\n=== Test Complete ===\n");

    free(vm);
    free(program);
    return 0;
}
