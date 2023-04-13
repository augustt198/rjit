#include "rjit.h"

#include <stdio.h>
#include <stdlib.h>

#define REG_IDX 3
#define REG_SPTR 4
#define REG_CHAR 5
#define REG_CMPRES 6
#define REG_LEN 7

#define REG_SP 31

#define COND_EQ 0b0000
#define COND_NE 0b0001
#define COND_GE 0b1010
#define COND_LT 0b1011
#define COND_GT 0b1100
#define COND_LE 0b1101

#define LABEL_NOMATCH 1
#define LABEL_MATCH 2

arm_inst_t arm_ldr_reg(reg_t base, reg_t offset, reg_t dest) {
    return 0xf8600800 | (base << 5) | (offset << 16) | (dest << 0);
}

arm_inst_t arm_add_reg(reg_t a, reg_t b, reg_t dest) {
    return 0x8b000000 | (a << 5) | (b << 16) | (dest << 0);
}

arm_inst_t arm_add_imm(reg_t a, int imm, reg_t dest) {
    return 0x91000000 | (a << 5) | (imm << 10) | (dest << 0);
}

arm_inst_t arm_sub_reg(reg_t a, reg_t b, reg_t dest) {
    return 0xcb000000 | (a << 5) | (b << 16) | (dest << 0);
}

arm_inst_t arm_sub_imm(reg_t a, int imm, reg_t dest) {
    return 0xd1000000 | (a << 5) | (imm << 10) | (dest << 0);
}

arm_inst_t arm_b_cond(int label, int cond) {
    return 0x54000000 | (label << 5) | cond;
}

arm_inst_t arm_b(int label) {
    return 0x14000000 | label;
}

int insert(arm_program_t *prog, arm_inst_t inst) {
    prog->insts[prog->index] = inst;
    return prog->index++;
}

void vm2arm(vm_program_t *vp, arm_program_t *ap) {
    FILE *f = ap->f;

    fprintf(f, "_matchit:\n");

    fprintf(f, "sub sp, sp, #64\n");
    fprintf(f, "stp x29, x30, [sp, #48]\n");

    fprintf(f, "mov x10, x0\n");
    fprintf(f, "mov x11, #0\n");
    fprintf(f, "mov x15, sp\n"); // thread stack

    for (int idx = 0; idx < vp->insts_length; idx++) {
        vm_inst_t vi = vp->insts[idx];

        for (int label_idx = 0; label_idx < vp->current_label; label_idx++) {
            if (vp->label_table[label_idx] == idx) {
                fprintf(f, "RL_%d:\n", label_idx);
            }
        }

        if (vi.op == OP_LITERAL) {
            char chr = vi.literal.str[0];
            fprintf(f, "ldrb w9, [x10, x11]\n");
            fprintf(f, "subs x9, x9, #%d\n", (int) chr);
            fprintf(f, "b.ne NOMATCH\n");
            fprintf(f, "add x11, x11, #1\n");

        } else if (vi.op == OP_MATCH) {
            fprintf(f, "ldrb w9, [x10, x11]\n");
            fprintf(f, "cbz w9, MATCH\n");
            fprintf(f, "b NOMATCH\n");
        } else if (vi.op == OP_JMP) {
            fprintf(f, "b RL_%d\n", vi.jmp_label);
        } else if (vi.op == OP_SPLIT) {
            fprintf(f, "adr x13, RL_%d\n", vi.split.label_1);
            fprintf(f, "adr x14, RL_%d\n", vi.split.label_2);
            fprintf(f, "b AddAThread\n");

        } else {
            printf("Unsupported\n");
            //exit(-1);
        }
    }

    fprintf(f, "AddAThread:\n");
    fprintf(f, "sub x15, x15, #16\n");
    fprintf(f, "stp x14, x11, [x15]\n");
    fprintf(f, "br x13\n");

    fprintf(f, "RunAThread:\n");
    fprintf(f, "ldp x14, x11, [x15]\n");
    fprintf(f, "add x15, x15, #16\n");
    fprintf(f, "br x14\n");

    fprintf(f, "NOMATCH:\n");
    fprintf(f, "cmp sp, x15\n");
    fprintf(f, "b.ne RunAThread\n");
    fprintf(f, "mov x12, #0\n");
    fprintf(f, "b FIN\n");

    fprintf(f, "MATCH:\n");
    fprintf(f, "mov x12, #1\n");
    fprintf(f, "b FIN\n");

    fprintf(f, "FIN:\n");
    fprintf(f, "ldp x29, x30, [sp, #48]\n");
    fprintf(f, "add sp, sp, #64\n");
    fprintf(f, "mov x0, x12\n");
    fprintf(f, "ret\n");
}
