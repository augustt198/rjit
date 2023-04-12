#include "rjit.h"

#include <stdio.h>
#include <sys/mman.h>

void print_node(regex_node_t *node) {
    if (node->tag == NODE_LITERAL) {
        printf("%.*s", node->literal.length, node->literal.str);
    } else if (node->tag == NODE_ANY) {
        printf(".");
    } else if (node->tag == NODE_SEQUENCE || node->tag == NODE_ALTERNATE) {
        printf("(");

        for (int i = 0; i < node->sequence.length; i++) {
            print_node(node->sequence.list[i]);
            if (node->tag == NODE_ALTERNATE && i != node->sequence.length - 1)
                printf("|");
        }

        printf(")");
    } else if (node->tag == NODE_REPEAT) {
        print_node(node->repeat.el);
        if (node->repeat.min == 0 && node->repeat.max == 1)
            printf("?");
        else if (node->repeat.min == 0 && node->repeat.max == -1)
            printf("*");
        else if (node->repeat.min == 1 && node->repeat.max == -1)
            printf("+");
    }
}

void print_node_tree(regex_node_t *node, int level) {
    printf("%*s", 4 * level, "");
    if (node->tag == NODE_LITERAL) {
        printf("literal '%.*s'\n", node->literal.length, node->literal.str);
    } else if (node->tag == NODE_ANY) {
        printf("any .\n");
    } else if (node->tag == NODE_SEQUENCE || node->tag == NODE_ALTERNATE) {
        printf("%s\n", node->tag == NODE_SEQUENCE ? "sequence" : "alternate");

        for (int i = 0; i < node->sequence.length; i++) {
            print_node_tree(node->sequence.list[i], level + 1);
        }
    } else if (node->tag == NODE_REPEAT) {
        printf("repeat [%d, %d]\n", node->repeat.min, node->repeat.max);
        print_node_tree(node->repeat.el, level + 1);
    }
}

void print_program(vm_program_t *prog) {
    printf("________________________ (program)\n");
    for (int i = 0; i < prog->insts_length; i++) {
        int label = -1;
        for (int j = 0; j < prog->current_label; j++) {
            if (prog->label_table[j] == i) label = j;
        }

        printf("%03d ", i);
        if (label >= 0) printf("%4d: ", label);
        else printf("      ");

        vm_inst_t inst = prog->insts[i];
        if (inst.op == OP_LITERAL) {
            printf("literal '%.*s'", inst.literal.length, inst.literal.str);
        } else if (inst.op == OP_JMP) {
            printf("jmp %d", inst.jmp_label);
        } else if (inst.op == OP_SPLIT) {
            printf("split %d, %d", inst.split.label_1, inst.split.label_2);
        } else if (inst.op == OP_ANY) {
            printf("any");
        } else if (inst.op == OP_MATCH) {
            printf("match");
        }
        printf("\n");
    }
}

void *executable_mem(int size) {
    return mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON, -1, 0);
}