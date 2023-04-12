#include "rjit.h"

#include <inttypes.h>
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


typedef int reg_t;
typedef uint32_t arm_inst_t;

typedef struct {
    arm_inst_t *insts;
    int index;

} arm_program_t;

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
    for (int idx = 0; idx < vp->insts_length; idx++) {
        vm_inst_t vi = vp->insts[idx];

        if (vi.op == OP_LITERAL) {
            char chr = vi.literal.str[0];
            insert(ap, arm_ldr_reg(REG_SPTR, REG_IDX, REG_CHAR));
            insert(ap, arm_sub_imm(REG_CHAR, (int) chr, REG_CHAR));
            insert(ap, arm_b_cond(LABEL_NOMATCH, COND_NE));
            insert(ap, arm_add_imm(REG_IDX, 1, REG_IDX));

        } else if (vi.op == OP_MATCH) {
            insert(ap, arm_sub_reg(REG_IDX, REG_LEN, REG_CMPRES));
            insert(ap, arm_b_cond(LABEL_MATCH, COND_EQ));
            insert(ap, arm_b(LABEL_NOMATCH));
        } else {
            printf("Unsupported\n");
            exit(-1);
        }
    }
}