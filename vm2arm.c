#include "rjit.h"

#include <stdio.h>
#include <stdlib.h>

#define REG_TMP2 "x3"
#define REG_TMP "x4"
#define REGW_TMP "w4"
#define REG_SPTR "x5"
#define REG_SIDX "x6"
#define REGW_SIDX "w6"
#define REG_CHAR "x7"
#define REGW_CHAR "w7"
#define REG_CURR_BASE "x8"
#define REG_CURR_LEN "x9"
#define REG_CURR_IDX "x10"
#define REG_RUN_PC "x11"

#define REG_NEXT_BASE "x12"
#define REG_NEXT_IDX "x13"
#define REG_HIST_BASE "x14"

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

    int N = vp->insts_length;
    int sp_sub = 16 + 3 * 8 * N;
    
    sp_sub = sp_sub + (16 - (sp_sub % 16)); // 16 byte aligned stack pointer

    fprintf(f, "_matchit:\n");

    // set up SP
    fprintf(f, "sub sp, sp, #%d\n", sp_sub);
    //fprintf(f, "stp x29, x30, [sp, #%d]\n", sp_sub - 16);
    fprintf(f, "str x29, [sp, #%d]\n", sp_sub-16); // offset can be too large for stp
    fprintf(f, "str x30, [sp, #%d]\n", sp_sub-8);

    // initialize our regs
    fprintf(f, "mov " REG_SPTR ", x0\n");
    fprintf(f, "mov x0, #0\n"); // the result (no match by default)
    fprintf(f, "mov " REG_SIDX ", #0\n");
    fprintf(f, "mov " REG_CURR_BASE ", sp\n" );
    fprintf(f, "mov " REG_CURR_IDX ", #0\n");
    fprintf(f, "mov " REG_CURR_LEN ", #1\n");
    fprintf(f, "mov " REG_NEXT_BASE ", sp\n");
    fprintf(f, "add " REG_NEXT_BASE ", " REG_NEXT_BASE ", #%d\n", 8*N);
    fprintf(f, "mov " REG_NEXT_IDX ", #0\n");
    fprintf(f, "mov " REG_HIST_BASE ", sp\n");
    fprintf(f, "add " REG_HIST_BASE ", " REG_HIST_BASE ", #%d\n", 2*8*N);

    // zero the history array
    fprintf(f, "mov " REG_TMP ", #0\n");
    fprintf(f, "zero_hist_loop:\n");
    fprintf(f, "mov " REG_TMP2 ", #-1\n");
    fprintf(f, "str " REG_TMP2 ", [" REG_HIST_BASE ", " REG_TMP ", sxtx #3]\n");
    fprintf(f, "add " REG_TMP ", " REG_TMP ", #1\n");
    fprintf(f, "cmp " REG_TMP ", #%d\n", N);
    fprintf(f, "b.lt zero_hist_loop\n");

    // add the first instruction to the current stack
    fprintf(f, "adr " REG_TMP ", bytecode_inst_0\n");
    fprintf(f, "str " REG_TMP ", [" REG_CURR_BASE "]\n");

    // the main loop
    fprintf(f, "the_loop:\n");
    // char = str[idx]
    fprintf(f, "ldrb " REGW_CHAR ", [" REG_SPTR ", " REG_SIDX "]\n");

    // stack is empty -> no possible matches, exit
    fprintf(f, "cmp " REG_CURR_LEN ", #0\n");
    fprintf(f, "b.eq FIN\n");

    // execute things on the stack
    fprintf(f, "loop_inner:\n");
    fprintf(f, "ldr " REG_RUN_PC ", [" REG_CURR_BASE ", " REG_CURR_IDX ", sxtx #3]\n");
    fprintf(f, "br " REG_RUN_PC "\n"); // do it!
    fprintf(f, "bytecode_instr_done:\n");
    fprintf(f, "add " REG_CURR_IDX ", " REG_CURR_IDX ", #1\n");
    fprintf(f, "cmp " REG_CURR_IDX ", " REG_CURR_LEN "\n");
    fprintf(f, "b.lt loop_inner\n");

    // inner loop is over, swap current & next stacks
    fprintf(f, "mov " REG_TMP ", " REG_CURR_BASE "\n");
    fprintf(f, "mov " REG_CURR_BASE ", " REG_NEXT_BASE "\n");
    fprintf(f, "mov " REG_NEXT_BASE ", " REG_TMP "\n");
    
    fprintf(f, "mov " REG_CURR_IDX ", #0\n");
    fprintf(f, "mov " REG_CURR_LEN ", " REG_NEXT_IDX "\n");
    fprintf(f, "mov " REG_NEXT_IDX ", #0\n");

    // go next to char, or exit without matching if already at '\0'
    fprintf(f, "add " REG_SIDX ", " REG_SIDX ", #1\n");
    fprintf(f, "cmp " REG_CHAR ", #0\n");
    fprintf(f, "b.ne the_loop\n");
    fprintf(f, "b FIN\n"); // we're done

    for (int idx = 0; idx < vp->insts_length; idx++) {
        vm_inst_t vi = vp->insts[idx];

        for (int label_idx = 0; label_idx < vp->current_label; label_idx++) {
            if (vp->label_table[label_idx] == idx) {
                fprintf(f, "RL_%d:\n", label_idx);
            }
        }
        fprintf(f, "bytecode_inst_%d:\n", idx);

        if (vi.op == OP_LITERAL || vi.op == OP_ANY) {
            if (vi.op == OP_LITERAL) {
                char chr = vi.literal.str[0];
                // assume char is already loaded
                //fprintf(f, "ldrb " REGW_CHAR ", [" REG_SPTR ", " REG_SIDX "]\n");
                fprintf(f, "cmp " REG_CHAR ", #%d\n", (int) chr);
                fprintf(f, "b.ne bytecode_instr_done\n");
            }

            fprintf(f, "ldrh " REGW_TMP ", [" REG_HIST_BASE ", #%d]\n", (idx+1)*8 + 4);
            fprintf(f, "cmp " REG_TMP ", " REG_SIDX "\n");
            fprintf(f, "b.eq bytecode_instr_done\n"); // this was already on the stack
            // or make these conditional instead of branching?
            fprintf(f, "strh " REGW_SIDX ", [" REG_HIST_BASE ", #%d]\n", (idx+1)*8 + 4);
            fprintf(f, "adr " REG_TMP ", bytecode_inst_%d\n", idx+1);
            fprintf(f, "str " REG_TMP ", [" REG_NEXT_BASE ", " REG_NEXT_IDX ", sxtx #3]\n");
            fprintf(f, "add " REG_NEXT_IDX ", " REG_NEXT_IDX ", #1\n");
            fprintf(f, "b bytecode_instr_done\n");

        } else if (vi.op == OP_MATCH) {
            fprintf(f, "cbz " REGW_CHAR ", MATCH\n");
            fprintf(f, "b bytecode_instr_done\n");

        } else if (vi.op == OP_JMP) {
            int jmp_pc = vp->label_table[vi.jmp_label];

            fprintf(f, "ldrh " REGW_TMP ", [" REG_HIST_BASE ", #%d]\n", jmp_pc*8);
            fprintf(f, "cmp " REG_TMP ", " REG_SIDX "\n");
            fprintf(f, "b.eq bytecode_instr_done\n"); // this was already on the stack
            // or make these conditional instead of branching?
            fprintf(f, "strh " REGW_SIDX ", [" REG_HIST_BASE ", #%d]\n", jmp_pc*8);
            fprintf(f, "adr " REG_TMP ", bytecode_inst_%d\n", jmp_pc);
            fprintf(f, "str " REG_TMP ", [" REG_CURR_BASE ", " REG_CURR_LEN ", sxtx #3]\n");
            fprintf(f, "add " REG_CURR_LEN ", " REG_CURR_LEN ", #1\n");
            fprintf(f, "b bytecode_instr_done\n");


        } else if (vi.op == OP_SPLIT) {
            int pc1 = vp->label_table[vi.split.label_1];
            int pc2 = vp->label_table[vi.split.label_2];

            fprintf(f, "ldrh " REGW_TMP ", [" REG_HIST_BASE ", #%d]\n", pc1*8);
            fprintf(f, "cmp " REG_TMP ", " REG_SIDX "\n");
            fprintf(f, "b.eq split_part2_for_%d\n", idx); // this was already on the stack
            // or make these conditional instead of branching?
            fprintf(f, "strh " REGW_SIDX ", [" REG_HIST_BASE ", #%d]\n", pc1*8);
            fprintf(f, "adr " REG_TMP ", bytecode_inst_%d\n", pc1);
            fprintf(f, "str " REG_TMP ", [" REG_CURR_BASE ", " REG_CURR_LEN ", sxtx #3]\n");
            fprintf(f, "add " REG_CURR_LEN ", " REG_CURR_LEN ", #1\n");

            fprintf(f, "split_part2_for_%d:\n", idx);

            fprintf(f, "ldrh " REGW_TMP ", [" REG_HIST_BASE ", #%d]\n", pc2*8);
            fprintf(f, "cmp " REG_TMP ", " REG_SIDX "\n");
            fprintf(f, "b.eq bytecode_instr_done\n"); // this was already on the stack
            // or make these conditional instead of branching?
            fprintf(f, "strh " REGW_SIDX ", [" REG_HIST_BASE ", #%d]\n", pc2*8);
            fprintf(f, "adr " REG_TMP ", bytecode_inst_%d\n", pc2);
            fprintf(f, "str " REG_TMP ", [" REG_CURR_BASE ", " REG_CURR_LEN ", sxtx #3]\n");
            fprintf(f, "add " REG_CURR_LEN ", " REG_CURR_LEN ", #1\n");
            fprintf(f, "b bytecode_instr_done\n");

        } else {
            printf("Unsupported\n");
            //exit(-1);
        }
    }

    // yay!
    fprintf(f, "MATCH:\n");
    fprintf(f, "mov x0, #1\n");

    fprintf(f, "FIN:\n");
    // restore SP
    // fprintf(f, "ldp x29, x30, [sp, #%d]\n", sp_sub - 16);
    fprintf(f, "ldr x29, [sp, #%d]\n", sp_sub-16);
    fprintf(f, "ldr x30, [sp, #%d]\n", sp_sub-8);
    fprintf(f, "add sp, sp, #%d\n", sp_sub);
    fprintf(f, "ret\n");
}
