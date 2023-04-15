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

    vm_thread_t *stack = (vm_thread_t*) malloc(4096 * sizeof(vm_thread_t));
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
    vm_thread_t *stack = (vm_thread_t*) malloc(sz * sizeof(vm_thread_t));
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

// thompson
bool vm_run3(vm_program_t *prog, const char *str) {
    int N = prog->insts_length;

    int histc[N];
    int histn[N];

    for (int i = 0; i < N; i++)
        histc[i] = histn[i] = -1;

    int buf1[N];
    int buf2[N];

    int *curr = buf1, *next = buf2;

    int currlen = 1;
    int nextidx = 0;
    curr[0] = 0;

    int g = 0;
    for (const char *sp = str; ; sp++, g++) {
        if (currlen == 0) return false;

        char c = *sp;
        for (int i = 0; i < currlen; i++) {
            int pc1, pc2;
            int idx = curr[i];
            vm_inst_t inst = prog->insts[idx];
            switch (inst.op) {
            case OP_LITERAL:
                if (*inst.literal.str == c) {
                    if (histn[idx+1] != g) {
                        next[nextidx++] = idx+1;
                        histn[idx+1] = g;
                    }
                }
                break;

            case OP_ANY:
                if (histn[idx+1] != g) {
                    next[nextidx++] = idx+1;
                    histn[idx+1] = g;
                }
                break;

            case OP_MATCH:
                if (c == '\0') return true;

            case OP_JMP:
                pc1 = prog->label_table[inst.jmp_label];
                if (histc[pc1] != g) {
                    curr[currlen++] = pc1;
                    histc[pc1] = g;
                }
                break;

            case OP_SPLIT:
                pc1 = prog->label_table[inst.split.label_1];
                pc2 = prog->label_table[inst.split.label_2];
                if (histc[pc1] != g) {
                    curr[currlen++] = pc1;
                    histc[pc1] = g;
                }
                if (histc[pc2] != g) {
                    curr[currlen++] = pc2;
                    histc[pc2] = g;
                }
                break;
            }
        }

        int *tmp = next;
        next = curr;
        curr = tmp;

        currlen = nextidx;
        nextidx = 0;

        if (c == '\0') break;
    }

    return false;
}

bool vm_run(vm_program_t *prog, const char *str) {
    return vm_run3(prog, str);
}
