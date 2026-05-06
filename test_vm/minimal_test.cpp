#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdarg.h>

// Include mock headers first to avoid conflicts
#include "Arduino.h"
#include "common.h"
#include "module_runtime.h"

// Define the Serial object
MockSerial Serial;

// Now include the actual compiler and VM source
#include "../src/bottle_error.cpp"
#include "../src/bottle_compiler.cpp"
#include "bottle_builtins_mock.cpp"
#include "../src/bottle_vm.cpp"

int main() {
    printf("=== Bottle VM Native Test ===\n");
    printf("BOTTLE_MAX_INSTRUCTIONS_PER_FRAME = %d\n\n", BOTTLE_MAX_INSTRUCTIONS_PER_FRAME);

    // Read the script from file
    FILE* f = fopen("../modules_dev/rhythm_spectrum/main.bottle", "r");
    if (!f) {
        printf("ERROR: Could not open main.bottle\n");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* test_script = (char*)malloc(size + 1);
    fread(test_script, 1, size, f);
    test_script[size] = '\0';
    fclose(f);

    printf("Testing script from main.bottle (%ld bytes)\n\n", size);
    printf("=== Compilation Output ===\n");

    bottle_program_t* program = (bottle_program_t*)malloc(sizeof(bottle_program_t));
    memset(program, 0, sizeof(*program));

    bool result = bottle_compile(test_script, program);

    printf("\n=== Compilation Result ===\n");
    if (result) {
        printf("Compilation SUCCEEDED\n");
        printf("Bytecode size: %d bytes\n", program->bytecode_size);
        printf("Constants: %d\n", program->constant_count);
        printf("Arrays: %d\n", program->array_count);
        printf("Scalars: %d\n", program->scalar_count);
        printf("Configs: %d\n", program->config_count);
        printf("Setup offset: %d (has_setup=%d)\n", program->setup_offset, program->has_setup);
        printf("Loop offset: %d (has_loop=%d)\n", program->loop_offset, program->has_loop);
        printf("Unload offset: %d (has_unload=%d)\n", program->unload_offset, program->has_unload);

        // Disassemble bytecode for debugging
        printf("\n=== BYTECODE DISASSEMBLY ===\n");
        for (int pc = 0; pc < program->bytecode_size; ) {
            uint8_t op = program->bytecode[pc];
            printf("%4d: 0x%02X ", pc, op);

            // Show opcode name and operands
            switch(op) {
                case 0x01: printf("PUSH_CONST %d\n", program->bytecode[pc+1]); pc += 2; break;
                case 0x02: printf("PUSH_SCALAR %d\n", program->bytecode[pc+1]); pc += 2; break;
                case 0x04: printf("POP_SCALAR %d\n", program->bytecode[pc+1]); pc += 2; break;
                case 0x05: printf("POP_ARRAY %d\n", program->bytecode[pc+1]); pc += 2; break;
                case 0x07: printf("POP\n"); pc++; break;
                case 0x10: printf("ADD\n"); pc++; break;
                case 0x11: printf("SUB\n"); pc++; break;
                case 0x12: printf("NEG\n"); pc++; break;
                case 0x18: printf("MUL\n"); pc++; break;
                case 0x19: printf("DIV\n"); pc++; break;
                case 0x20: printf("LT\n"); pc++; break;
                case 0x21: printf("LE\n"); pc++; break;
                case 0x22: printf("GT\n"); pc++; break;
                case 0x23: printf("GE\n"); pc++; break;
                case 0x24: printf("EQ\n"); pc++; break;
                case 0x25: printf("NE\n"); pc++; break;
                case 0x28: printf("AND\n"); pc++; break;
                case 0x29: printf("OR\n"); pc++; break;
                case 0x2A: printf("NOT\n"); pc++; break;
                case 0x30: {
                    uint16_t offset = (program->bytecode[pc+1] << 8) | program->bytecode[pc+2];
                    int16_t signed_offset = (int16_t)offset;
                    printf("JUMP %d (raw=0x%04X, target=%d)\n", signed_offset, offset, pc + 3 + signed_offset);
                    pc += 3;
                    break;
                }
                case 0x31: {
                    uint16_t offset = (program->bytecode[pc+1] << 8) | program->bytecode[pc+2];
                    int16_t signed_offset = (int16_t)offset;
                    printf("JUMP_IF_FALSE %d (raw=0x%04X, target=%d)\n", signed_offset, offset, pc + 3 + signed_offset);
                    pc += 3;
                    break;
                }
                case 0x32: {
                    uint16_t offset = (program->bytecode[pc+1] << 8) | program->bytecode[pc+2];
                    int16_t signed_offset = (int16_t)offset;
                    printf("JUMP_IF_TRUE %d (raw=0x%04X, target=%d)\n", signed_offset, offset, pc + 3 + signed_offset);
                    pc += 3;
                    break;
                }
                case 0x40: printf("CALL_MAX\n"); pc++; break;
                case 0x41: printf("CALL_MIN\n"); pc++; break;
                case 0x58: printf("CALL_MILLIS\n"); pc++; break;
                default: printf("UNKNOWN\n"); pc++; break;
            }
        }
        printf("=== END DISASSEMBLY ===\n\n");

        // Calculate bytecode checksum
        uint32_t checksum = 0;
        for (int i = 0; i < program->bytecode_size; i++) {
            checksum += program->bytecode[i];
        }
        printf("Bytecode checksum: 0x%08X\n", checksum);
        printf("First 10 bytes: ");
        for (int i = 0; i < 10 && i < program->bytecode_size; i++) {
            printf("%02X ", program->bytecode[i]);
        }
        printf("\n");
        printf("Bytes at PC=467: ");
        for (int i = 467; i < 477 && i < program->bytecode_size; i++) {
            printf("%02X ", program->bytecode[i]);
        }
        printf("\n\n");

        // Now test VM execution
        printf("\n=== VM Execution Test ===\n");

        bottle_vm_t* vm = (bottle_vm_t*)malloc(sizeof(bottle_vm_t));
        bottle_vm_init(vm, program);

        // Debug: Print memory addresses
        printf("\n=== MEMORY LAYOUT ===\n");
        printf("program address: %p\n", (void*)program);
        printf("program->bytecode address: %p\n", (void*)&program->bytecode[0]);
        printf("program->bytecode[467] address: %p\n", (void*)&program->bytecode[467]);
        printf("program size: %zu bytes\n", sizeof(*program));
        printf("vm address: %p\n", (void*)vm);
        printf("vm->int_arrays address: %p\n", (void*)&vm->int_arrays[0][0]);
        printf("vm->scalars address: %p\n", (void*)&vm->scalars[0]);
        printf("vm->stack address: %p\n", (void*)&vm->stack[0]);
        printf("vm->stack[127] address: %p\n", (void*)&vm->stack[127]);
        printf("vm->stack[128] address (OUT OF BOUNDS): %p\n", (void*)&vm->stack[128]);
        printf("vm size: %zu bytes\n", sizeof(*vm));
        printf("bottle_value_t size: %zu bytes\n", sizeof(bottle_value_t));
        printf("Distance between program and vm: %td bytes\n",
               (char*)vm - (char*)program);
        printf("Distance from vm->stack[128] to bytecode[467]: %td bytes\n",
               (char*)&program->bytecode[467] - (char*)&vm->stack[128]);
        printf("\n");

        // Create mock context
        module_context_t ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.now_ms = 0;

        // Mock spectrum data
        for (int i = 0; i < MATRIX_WIDTH; i++) {
            ctx.sensor.spectrum[i] = (i * 10) % 256;
        }

        // Run loop multiple times to detect stack leak
        printf("Running loop 10 times to detect stack leaks...\n");
        for (int frame = 0; frame < 10; frame++) {
            ctx.now_ms += 16; // Simulate 16ms per frame

            printf("\n--- Frame %d (t=%d ms) ---\n", frame, ctx.now_ms);
            bottle_vm_run_loop(vm, program, &ctx);

            // Verify bytecode checksum after execution
            uint32_t checksum_after = 0;
            for (int i = 0; i < program->bytecode_size; i++) {
                checksum_after += program->bytecode[i];
            }
            printf("Checksum after frame %d: 0x%08X\n", frame, checksum_after);
            printf("Bytes at PC=467 after frame %d: ", frame);
            for (int i = 467; i < 477 && i < program->bytecode_size; i++) {
                printf("%02X ", program->bytecode[i]);
            }
            printf("\n");

            if (vm->error.has_error) {
                printf("ERROR in frame %d: %s\n", frame, vm->error.message);
                break;
            }

            printf("Frame %d completed successfully, final stack_top=%d\n", frame, vm->stack_top);
        }

        free(vm);
        free(program);

    } else {
        printf("Compilation FAILED\n");
        free(program);
    }

    free(test_script);
    return result ? 0 : 1;
}
