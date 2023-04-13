#include "rjit.h"

#include <stdlib.h>

// Some ways to implement the VM, good for prototyping
// and timing against JIT

typedef struct {
    uint64_t pc;
    uint64_t idx;
} vm_thread_t;

bool vm_run1(vm_program_t *prog, const char *str) {
    vm_thread_t thr = {.pc = 0, .idx = 0};

    vm_thread_t *stack = malloc(4096 * sizeof(vm_thread_t));
    int stackpos = 0;
    int stackmax = 0;

    while (true) {
        printf("pc = %llu, idx = %llu (%c)\n", thr.pc, thr.idx, str[thr.idx]);
        vm_inst_t inst = prog->insts[thr.pc];
        if (inst.op == OP_LITERAL) {
            if (*inst.literal.str == str[thr.idx]) {
                thr.pc++;
                thr.idx++;
                continue;
            }
        } else if (inst.op == OP_MATCH) {
            if (str[thr.idx] == '\0') {
                printf("Stack max: %d\n", stackmax);
                return true;
            }
        } else if (inst.op == OP_JMP) {
            thr.pc = prog->label_table[inst.jmp_label];
            continue;
        } else if (inst.op == OP_SPLIT) {
            uint64_t pc1 = prog->label_table[inst.split.label_1];
            uint64_t pc2 = prog->label_table[inst.split.label_2];
            stack[stackpos] = (vm_thread_t){.pc = pc2, .idx = thr.idx};
            stackpos++;

            if (stackpos > stackmax) stackmax = stackpos;

            thr.pc = pc1;
            continue;
        }

        // if we fall through we should pop a thread
        if (stackpos == 0) {
            printf("Stack max: %d\n", stackmax);
            return false;
        }

        thr = stack[stackpos - 1];
        stackpos--;
    }

    // unreachable?
    return false;
}

int posmod(int i, int n) {
    return (i % n + n) % n;
}

bool vm_run2(vm_program_t *prog, const char *str) {
    vm_thread_t thr = {.pc = 0, .idx = 0};

    const int sz = 15;
    vm_thread_t *stack = malloc(sz * sizeof(vm_thread_t));
    int stackstart = 0, stackend = 0;

    int stackmax = 0;

    while (true) {
        printf("pc = %llu, idx = %llu (%c)\n", thr.pc, thr.idx, str[thr.idx]);
        vm_inst_t inst = prog->insts[thr.pc];
        if (inst.op == OP_LITERAL) {
            if (*inst.literal.str == str[thr.idx]) {
                thr.pc++;
                thr.idx++;
                continue;
            }
        } else if (inst.op == OP_MATCH) {
            if (str[thr.idx] == '\0') {
                printf("Stack max: %d\n", stackmax);
                return true;
            }
        } else if (inst.op == OP_JMP) {
            thr.pc = prog->label_table[inst.jmp_label];
            continue;
        } else if (inst.op == OP_SPLIT) {
            if (stackstart == stackend + 1 || (stackstart == 0 && stackend == sz - 1)) {
                // too full
                printf(">>>>>> too full!\n");
                vm_thread_t t2 = stack[stackstart];

                stackstart = (stackstart + 1) % sz;
                stackend = (stackend + 1) % sz;
                stack[posmod(stackend-1, sz)] = thr;

                thr = t2;
                continue;
            } else {

                printf("ooga\n");
                uint64_t pc1 = prog->label_table[inst.split.label_1];
                uint64_t pc2 = prog->label_table[inst.split.label_2];
                stack[stackend] = (vm_thread_t){.pc = pc2, .idx = thr.idx};
                stackend = (stackend + 1) % sz;

                int len = posmod(stackend - stackstart, sz);
                if (len > stackmax) stackmax = len;

                thr.pc = pc1;
                continue;
            }
        }

        // if we fall through we should pop a thread
        if (stackstart == stackend) {
            printf("Stack max: %d\n", stackmax);
            return false;
        }

        thr = stack[stackstart];
        stackstart = (stackstart + 1) % sz;
    }

    // unreachable?
    return false;
}

bool vm_run(vm_program_t *prog, const char *str) {
    return vm_run2(prog, str);
}
