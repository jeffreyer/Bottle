#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdarg.h>
#include <assert.h>

// Include mock headers first to avoid conflicts
#include "Arduino.h"
#include "common.h"
#include "module_runtime.h"

// Include VM headers (not source files - those are compiled separately)
#include "bottle_types.h"
#include "bottle_error.h"
#include "bottle_compiler.h"
#include "bottle_vm.h"

// Define the Serial object
MockSerial Serial;

// Test framework
int g_tests_passed = 0;
int g_tests_failed = 0;

#define TEST(name) \
    void test_##name(); \
    void run_test_##name() { \
        printf("\n=== TEST: %s ===\n", #name); \
        test_##name(); \
    } \
    void test_##name()

#define ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("FAIL: %s (line %d)\n", message, __LINE__); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_EQ(actual, expected, message) \
    do { \
        if ((actual) != (expected)) { \
            printf("FAIL: %s - expected %d, got %d (line %d)\n", message, (int)(expected), (int)(actual), __LINE__); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_FLOAT_EQ(actual, expected, tolerance, message) \
    do { \
        float diff = (actual) - (expected); \
        if (diff < 0) diff = -diff; \
        if (diff > (tolerance)) { \
            printf("FAIL: %s - expected %f, got %f (line %d)\n", message, (expected), (actual), __LINE__); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

#define PASS() \
    do { \
        printf("PASS\n"); \
        g_tests_passed++; \
    } while(0)

// Helper to compile and execute a script
bool compile_and_run(const char* script, bottle_program_t** out_program, bottle_vm_t** out_vm, module_context_t* ctx) {
    *out_program = (bottle_program_t*)malloc(sizeof(bottle_program_t));
    memset(*out_program, 0, sizeof(bottle_program_t));

    if (!bottle_compile(script, *out_program)) {
        printf("Compilation failed\n");
        return false;
    }

    *out_vm = (bottle_vm_t*)malloc(sizeof(bottle_vm_t));
    bottle_vm_init(*out_vm, *out_program);

    // Run setup if exists
    if ((*out_program)->has_setup) {
        bottle_vm_execute(*out_vm, *out_program, (*out_program)->setup_offset, ctx);
        if ((*out_vm)->error.has_error) {
            printf("Setup execution failed: %s\n", (*out_vm)->error.message);
            return false;
        }
    }

    // Run loop if exists
    if ((*out_program)->has_loop) {
        bottle_vm_execute(*out_vm, *out_program, (*out_program)->loop_offset, ctx);
        if ((*out_vm)->error.has_error) {
            printf("Loop execution failed: %s\n", (*out_vm)->error.message);
            return false;
        }
    }

    return true;
}

void cleanup(bottle_program_t* program, bottle_vm_t* vm) {
    if (program) free(program);
    if (vm) free(vm);
}

// ============================================================================
// UNIT TESTS
// ============================================================================

TEST(basic_arithmetic) {
    const char* script = R"(
        runtime bottle-vm@0.2
        module test.arithmetic
        state result = 0
        loop {
            result = 5 + 3
        }
    )";

    module_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    bottle_program_t* program = NULL;
    bottle_vm_t* vm = NULL;

    ASSERT(compile_and_run(script, &program, &vm, &ctx), "Compilation and execution");
    ASSERT_FLOAT_EQ(vm->scalars[0], 8.0f, 0.001f, "5 + 3 = 8");
    ASSERT_EQ(vm->stack_top, 0, "Stack should be empty after execution");

    cleanup(program, vm);
    PASS();
}

TEST(arithmetic_operations) {
    const char* script = R"(
        runtime bottle-vm@0.2
        module test.arithmetic
        state add = 0
        state sub = 0
        state mul = 0
        state div = 0
        state mod = 0
        state neg = 0
        loop {
            add = 10 + 5
            sub = 10 - 3
            mul = 4 * 3
            div = 20 / 4
            mod = 17 % 5
            neg = -(7)
        }
    )";

    module_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    bottle_program_t* program = NULL;
    bottle_vm_t* vm = NULL;

    ASSERT(compile_and_run(script, &program, &vm, &ctx), "Compilation and execution");
    ASSERT_FLOAT_EQ(vm->scalars[0], 15.0f, 0.001f, "10 + 5 = 15");
    ASSERT_FLOAT_EQ(vm->scalars[1], 7.0f, 0.001f, "10 - 3 = 7");
    ASSERT_FLOAT_EQ(vm->scalars[2], 12.0f, 0.001f, "4 * 3 = 12");
    ASSERT_FLOAT_EQ(vm->scalars[3], 5.0f, 0.001f, "20 / 4 = 5");
    ASSERT_FLOAT_EQ(vm->scalars[4], 2.0f, 0.001f, "17 % 5 = 2");
    ASSERT_FLOAT_EQ(vm->scalars[5], -7.0f, 0.001f, "-(7) = -7");

    cleanup(program, vm);
    PASS();
}

TEST(comparison_operators) {
    const char* script = R"(
        runtime bottle-vm@0.2
        module test.comparison
        state lt = 0
        state le = 0
        state gt = 0
        state ge = 0
        state eq = 0
        state ne = 0
        loop {
            lt = 5 < 10
            le = 5 <= 5
            gt = 10 > 5
            ge = 10 >= 10
            eq = 7 == 7
            ne = 7 != 8
        }
    )";

    module_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    bottle_program_t* program = NULL;
    bottle_vm_t* vm = NULL;

    ASSERT(compile_and_run(script, &program, &vm, &ctx), "Compilation and execution");
    ASSERT_FLOAT_EQ(vm->scalars[0], 1.0f, 0.001f, "5 < 10 = true");
    ASSERT_FLOAT_EQ(vm->scalars[1], 1.0f, 0.001f, "5 <= 5 = true");
    ASSERT_FLOAT_EQ(vm->scalars[2], 1.0f, 0.001f, "10 > 5 = true");
    ASSERT_FLOAT_EQ(vm->scalars[3], 1.0f, 0.001f, "10 >= 10 = true");
    ASSERT_FLOAT_EQ(vm->scalars[4], 1.0f, 0.001f, "7 == 7 = true");
    ASSERT_FLOAT_EQ(vm->scalars[5], 1.0f, 0.001f, "7 != 8 = true");

    cleanup(program, vm);
    PASS();
}

TEST(logical_operators) {
    const char* script = R"(
        runtime bottle-vm@0.2
        module test.logical
        state and_true = 0
        state and_false = 0
        state or_true = 0
        state or_false = 0
        state not_true = 0
        state not_false = 0
        loop {
            and_true = 1 && 1
            and_false = 1 && 0
            or_true = 1 || 0
            or_false = 0 || 0
            not_true = !0
            not_false = !1
        }
    )";

    module_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    bottle_program_t* program = NULL;
    bottle_vm_t* vm = NULL;

    ASSERT(compile_and_run(script, &program, &vm, &ctx), "Compilation and execution");
    ASSERT_FLOAT_EQ(vm->scalars[0], 1.0f, 0.001f, "1 && 1 = true");
    ASSERT_FLOAT_EQ(vm->scalars[1], 0.0f, 0.001f, "1 && 0 = false");
    ASSERT_FLOAT_EQ(vm->scalars[2], 1.0f, 0.001f, "1 || 0 = true");
    ASSERT_FLOAT_EQ(vm->scalars[3], 0.0f, 0.001f, "0 || 0 = false");
    ASSERT_FLOAT_EQ(vm->scalars[4], 1.0f, 0.001f, "!0 = true");
    ASSERT_FLOAT_EQ(vm->scalars[5], 0.0f, 0.001f, "!1 = false");

    cleanup(program, vm);
    PASS();
}

TEST(if_statement) {
    const char* script = R"(
        runtime bottle-vm@0.2
        module test.if
        state result = 0
        loop {
            if 5 > 3 {
                result = 10
            }
        }
    )";

    module_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    bottle_program_t* program = NULL;
    bottle_vm_t* vm = NULL;

    ASSERT(compile_and_run(script, &program, &vm, &ctx), "Compilation and execution");
    ASSERT_FLOAT_EQ(vm->scalars[0], 10.0f, 0.001f, "if true branch executed");
    ASSERT_EQ(vm->stack_top, 0, "Stack should be empty");

    cleanup(program, vm);
    PASS();
}

TEST(if_else_statement) {
    const char* script = R"(
        runtime bottle-vm@0.2
        module test.if_else
        state result1 = 0
        state result2 = 0
        loop {
            if 5 > 10 {
                result1 = 1
            } else {
                result1 = 2
            }
            if 10 > 5 {
                result2 = 3
            } else {
                result2 = 4
            }
        }
    )";

    module_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    bottle_program_t* program = NULL;
    bottle_vm_t* vm = NULL;

    ASSERT(compile_and_run(script, &program, &vm, &ctx), "Compilation and execution");
    ASSERT_FLOAT_EQ(vm->scalars[0], 2.0f, 0.001f, "else branch executed");
    ASSERT_FLOAT_EQ(vm->scalars[1], 3.0f, 0.001f, "if branch executed");
    ASSERT_EQ(vm->stack_top, 0, "Stack should be empty");

    cleanup(program, vm);
    PASS();
}

TEST(if_else_if_chain) {
    const char* script = R"(
        runtime bottle-vm@0.2
        module test.if_else_if
        state result = 0
        loop {
            if 5 > 10 {
                result = 1
            } else if 5 > 7 {
                result = 2
            } else if 5 > 3 {
                result = 3
            } else {
                result = 4
            }
        }
    )";

    module_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    bottle_program_t* program = NULL;
    bottle_vm_t* vm = NULL;

    ASSERT(compile_and_run(script, &program, &vm, &ctx), "Compilation and execution");
    ASSERT_FLOAT_EQ(vm->scalars[0], 3.0f, 0.001f, "second else if branch executed");
    ASSERT_EQ(vm->stack_top, 0, "Stack should be empty");

    cleanup(program, vm);
    PASS();
}

TEST(array_operations) {
    const char* script = R"(
        runtime bottle-vm@0.2
        module test.array
        state arr[5] = 0
        state sum = 0
        loop {
            arr[0] = 10
            arr[1] = 20
            arr[2] = 30
            sum = arr[0] + arr[1] + arr[2]
        }
    )";

    module_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    bottle_program_t* program = NULL;
    bottle_vm_t* vm = NULL;

    ASSERT(compile_and_run(script, &program, &vm, &ctx), "Compilation and execution");
    ASSERT_EQ(vm->arrays[0][0], 10, "arr[0] = 10");
    ASSERT_EQ(vm->arrays[0][1], 20, "arr[1] = 20");
    ASSERT_EQ(vm->arrays[0][2], 30, "arr[2] = 30");
    ASSERT_FLOAT_EQ(vm->scalars[0], 60.0f, 0.001f, "sum = 60");
    ASSERT_EQ(vm->stack_top, 0, "Stack should be empty");

    cleanup(program, vm);
    PASS();
}

TEST(for_loop_array) {
    const char* script = R"(
        runtime bottle-vm@0.2
        module test.for_array
        state arr[5] = 0
        state sum = 0
        loop {
            arr[0] = 1
            arr[1] = 2
            arr[2] = 3
            arr[3] = 4
            arr[4] = 5
            for i in arr {
                sum = sum + arr[i]
            }
        }
    )";

    module_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    bottle_program_t* program = NULL;
    bottle_vm_t* vm = NULL;

    ASSERT(compile_and_run(script, &program, &vm, &ctx), "Compilation and execution");
    ASSERT_FLOAT_EQ(vm->scalars[0], 15.0f, 0.001f, "sum = 1+2+3+4+5 = 15");
    ASSERT_EQ(vm->stack_top, 0, "Stack should be empty");

    cleanup(program, vm);
    PASS();
}

TEST(for_loop_range) {
    const char* script = R"(
        runtime bottle-vm@0.2
        module test.for_range
        state sum = 0
        loop {
            for i in range(0, 5) {
                sum = sum + i
            }
        }
    )";

    module_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    bottle_program_t* program = NULL;
    bottle_vm_t* vm = NULL;

    ASSERT(compile_and_run(script, &program, &vm, &ctx), "Compilation and execution");
    ASSERT_FLOAT_EQ(vm->scalars[0], 10.0f, 0.001f, "sum = 0+1+2+3+4 = 10");
    ASSERT_EQ(vm->stack_top, 0, "Stack should be empty");

    cleanup(program, vm);
    PASS();
}

TEST(nested_for_loops) {
    const char* script = R"(
        runtime bottle-vm@0.2
        module test.nested_for
        state count = 0
        loop {
            for i in range(0, 3) {
                for j in range(0, 3) {
                    count = count + 1
                }
            }
        }
    )";

    module_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    bottle_program_t* program = NULL;
    bottle_vm_t* vm = NULL;

    ASSERT(compile_and_run(script, &program, &vm, &ctx), "Compilation and execution");
    ASSERT_FLOAT_EQ(vm->scalars[0], 9.0f, 0.001f, "count = 3*3 = 9");
    ASSERT_EQ(vm->stack_top, 0, "Stack should be empty");

    cleanup(program, vm);
    PASS();
}

TEST(builtin_max_min) {
    const char* script = R"(
        runtime bottle-vm@0.2
        module test.max_min
        state max_val = 0
        state min_val = 0
        loop {
            max_val = max(5, 10)
            min_val = min(5, 10)
        }
    )";

    module_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    bottle_program_t* program = NULL;
    bottle_vm_t* vm = NULL;

    ASSERT(compile_and_run(script, &program, &vm, &ctx), "Compilation and execution");
    ASSERT_FLOAT_EQ(vm->scalars[0], 10.0f, 0.001f, "max(5, 10) = 10");
    ASSERT_FLOAT_EQ(vm->scalars[1], 5.0f, 0.001f, "min(5, 10) = 5");

    cleanup(program, vm);
    PASS();
}

TEST(builtin_clamp) {
    const char* script = R"(
        runtime bottle-vm@0.2
        module test.clamp
        state val1 = 0
        state val2 = 0
        state val3 = 0
        loop {
            val1 = clamp(5, 0, 10)
            val2 = clamp(-5, 0, 10)
            val3 = clamp(15, 0, 10)
        }
    )";

    module_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    bottle_program_t* program = NULL;
    bottle_vm_t* vm = NULL;

    ASSERT(compile_and_run(script, &program, &vm, &ctx), "Compilation and execution");
    ASSERT_FLOAT_EQ(vm->scalars[0], 5.0f, 0.001f, "clamp(5, 0, 10) = 5");
    ASSERT_FLOAT_EQ(vm->scalars[1], 0.0f, 0.001f, "clamp(-5, 0, 10) = 0");
    ASSERT_FLOAT_EQ(vm->scalars[2], 10.0f, 0.001f, "clamp(15, 0, 10) = 10");

    cleanup(program, vm);
    PASS();
}

TEST(temporal_statement) {
    const char* script = R"(
        runtime bottle-vm@0.2
        module test.temporal
        state counter = 0
        loop {
            counter = counter + 1 every 100ms
        }
    )";

    module_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    bottle_program_t* program = NULL;
    bottle_vm_t* vm = NULL;

    ASSERT(compile_and_run(script, &program, &vm, &ctx), "Compilation and execution");

    // First execution at t=0, should execute
    ASSERT_FLOAT_EQ(vm->scalars[0], 1.0f, 0.001f, "counter incremented at t=0");

    // Second execution at t=50ms, should NOT execute
    ctx.now_ms = 50;
    bottle_vm_execute(vm, program, program->loop_offset, &ctx);
    ASSERT_FLOAT_EQ(vm->scalars[0], 1.0f, 0.001f, "counter unchanged at t=50ms");

    // Third execution at t=100ms, should execute
    ctx.now_ms = 100;
    bottle_vm_execute(vm, program, program->loop_offset, &ctx);
    ASSERT_FLOAT_EQ(vm->scalars[0], 2.0f, 0.001f, "counter incremented at t=100ms");

    ASSERT_EQ(vm->stack_top, 0, "Stack should be empty");

    cleanup(program, vm);
    PASS();
}

TEST(stack_cleanup_after_frame) {
    const char* script = R"(
        runtime bottle-vm@0.2
        module test.stack_cleanup
        state arr[5] = 0
        state counter = 0
        loop {
            for i in arr {
                counter = counter + 1 every 50ms
            }
        }
    )";

    module_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    bottle_program_t* program = NULL;
    bottle_vm_t* vm = NULL;

    ASSERT(compile_and_run(script, &program, &vm, &ctx), "Compilation and execution");

    // Run multiple frames to check for stack leaks
    for (int frame = 0; frame < 10; frame++) {
        ctx.now_ms += 16;
        bottle_vm_execute(vm, program, program->loop_offset, &ctx);
        ASSERT_EQ(vm->stack_top, 0, "Stack should be empty after each frame");
    }

    cleanup(program, vm);
    PASS();
}

TEST(complex_expression) {
    const char* script = R"(
        runtime bottle-vm@0.2
        module test.complex_expr
        state result = 0
        loop {
            result = (5 + 3) * 2 - 10 / 2
        }
    )";

    module_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    bottle_program_t* program = NULL;
    bottle_vm_t* vm = NULL;

    ASSERT(compile_and_run(script, &program, &vm, &ctx), "Compilation and execution");
    ASSERT_FLOAT_EQ(vm->scalars[0], 11.0f, 0.001f, "(5+3)*2-10/2 = 16-5 = 11");

    cleanup(program, vm);
    PASS();
}

TEST(array_bounds_checking) {
    const char* script = R"(
        runtime bottle-vm@0.2
        module test.bounds
        state arr[5] = 0
        state result = 0
        loop {
            result = arr[10]
        }
    )";

    module_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    bottle_program_t* program = (bottle_program_t*)malloc(sizeof(bottle_program_t));
    memset(program, 0, sizeof(bottle_program_t));

    ASSERT(bottle_compile(script, program), "Compilation should succeed");

    bottle_vm_t* vm = (bottle_vm_t*)malloc(sizeof(bottle_vm_t));
    bottle_vm_init(vm, program);

    bottle_vm_execute(vm, program, program->loop_offset, &ctx);

    ASSERT(vm->error.has_error, "Should have array bounds error");

    cleanup(program, vm);
    PASS();
}

TEST(setup_and_loop) {
    const char* script = R"(
        runtime bottle-vm@0.2
        module test.setup_loop
        state counter = 0
        setup {
            counter = 100
        }
        loop {
            counter = counter + 1
        }
    )";

    module_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    bottle_program_t* program = NULL;
    bottle_vm_t* vm = NULL;

    ASSERT(compile_and_run(script, &program, &vm, &ctx), "Compilation and execution");
    ASSERT_FLOAT_EQ(vm->scalars[0], 101.0f, 0.001f, "setup sets 100, loop adds 1");

    cleanup(program, vm);
    PASS();
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

int main() {
    printf("==========================================================\n");
    printf("           BOTTLE VM UNIT TEST SUITE\n");
    printf("==========================================================\n");

    // Run all tests
    run_test_basic_arithmetic();
    run_test_arithmetic_operations();
    run_test_comparison_operators();
    run_test_logical_operators();
    run_test_if_statement();
    run_test_if_else_statement();
    run_test_if_else_if_chain();
    run_test_array_operations();
    run_test_for_loop_array();
    run_test_for_loop_range();
    run_test_nested_for_loops();
    run_test_builtin_max_min();
    run_test_builtin_clamp();
    run_test_temporal_statement();
    run_test_stack_cleanup_after_frame();
    run_test_complex_expression();
    run_test_array_bounds_checking();
    run_test_setup_and_loop();

    // Print summary
    printf("\n==========================================================\n");
    printf("TEST SUMMARY\n");
    printf("==========================================================\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    printf("Total:  %d\n", g_tests_passed + g_tests_failed);
    printf("==========================================================\n");

    return g_tests_failed > 0 ? 1 : 0;
}
