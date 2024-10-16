#include "rjit.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include <sys/mman.h>
#include <pthread.h>
#include <libkern/OSCacheControl.h>

#include <re2/re2.h>

regex_node_t *regex_node_allocate(regex_node_tag_t tag) {
    regex_node_t *node = (regex_node_t *) malloc(sizeof(regex_node_t));
    node->tag = tag;
    return node;
}

void regex_node_free(regex_node_t *node) { free(node); }

void parse_error(const char *msg) {
    printf("Oops: %s\n", msg);
    exit(-1);
}

regex_node_t *regex_parse(const char **pattern) {
    // linked list of nodes in this sequence
    regex_node_t *head = regex_node_allocate(NODE_NULL);
    regex_node_t *current = head;
    int seqlength = 0;

    char c = **pattern;
    while (c != '\0' && c != '|' && c != ')') {
        *pattern = *pattern + 1; // consume it

        regex_node_t *next = NULL;
        if (c == '(') {
            next = regex_parse(pattern);

            if (**pattern != ')') parse_error("Expected ')'");
            *pattern = *pattern + 1;

        } else if (isalpha(c) || isdigit(c)) {
            next = regex_node_allocate(NODE_LITERAL);
            next->literal.str = *pattern - 1;
            next->literal.length = 1;

        } else if (c == '.') {
            next = regex_node_allocate(NODE_ANY);

        } else if (c == '?' || c == '*' || c == '+') {
            if (current->tag == NODE_NULL || current->tag == NODE_REPEAT)
                parse_error("Cannot use repetition here");

            regex_node_t *el = regex_node_allocate(NODE_NULL);
            *el = *current; // copy current node

            current->tag = NODE_REPEAT; // change current into a repeat node
            current->repeat.el = el;

            if (c == '?') { current->repeat.min = 0; current->repeat.max = 1; }
            if (c == '*') { current->repeat.min = 0; current->repeat.max = -1; }
            if (c == '+') { current->repeat.min = 1; current->repeat.max = -1; }
        }

        if (next != NULL) { // did we add to the sequence?
            seqlength++;
            current->next = next;
            current = next;
        }

        c = **pattern; // peek
    }
    
    regex_node_t *seq = regex_node_allocate(NODE_SEQUENCE);
    seq->sequence.length = seqlength;
    seq->sequence.list = (regex_node_t **) malloc(seqlength * sizeof(regex_node_t *));

    head = head->next; // get rid of the null node
    for (int i = 0; head != NULL; i++, head = head->next) {
        seq->sequence.list[i] = head;
    }

    if (c == '|') { // make things simple: only two alternatives per node
        *pattern = *pattern + 1; // consume it

        regex_node_t *alt = regex_node_allocate(NODE_ALTERNATE);
        alt->sequence.length = 2;
        alt->sequence.list = (regex_node_t **) malloc(2 * sizeof(regex_node_t *));

        alt->sequence.list[0] = seq;
        alt->sequence.list[1] = regex_parse(pattern);

        return alt;
    }

    return seq;
}

// remove sequence nodes with a single child
regex_node_t *eliminate_single_seqs(regex_node_t *node) {
    if (node->tag == NODE_SEQUENCE && node->sequence.length == 1) {
        regex_node_t *ret = eliminate_single_seqs(node->sequence.list[0]);
        free(node);
        return ret;
    }

    if (node->tag == NODE_SEQUENCE || node->tag == NODE_ALTERNATE) {
        for (int i = 0; i < node->sequence.length; i++) {
            node->sequence.list[i] = eliminate_single_seqs(node->sequence.list[i]);
        }
    } else if (node->tag == NODE_REPEAT) {
        node->repeat.el = eliminate_single_seqs(node->repeat.el);
    }
    return node;
}

void compress_literals(regex_node_t *node) {
    if (node->tag == NODE_SEQUENCE) {
        regex_node_t *curr = NULL;
        for (int i = 0; i < node->sequence.length; i++) {
            regex_node_t *el = node->sequence.list[i];
            compress_literals(el);
            if (el->tag == NODE_LITERAL) {
                if (curr == NULL) {
                    curr = el;
                } else {
                    // curr ends at el's start
                    if (curr->literal.str + curr->literal.length == el->literal.str) {
                        curr->literal.length += el->literal.length;
                        el->tag = NODE_NULL; // this should be deleted
                    } else {
                        curr = el;
                    }
                }
            } else {
                curr = NULL;
            }
        }

        // delete the null nodes
        int idx = 0;
        for (int i = 0; i < node->sequence.length; i++) {
            if (node->sequence.list[i]->tag == NODE_NULL) {
                regex_node_free(node->sequence.list[i]);
            } else {
                node->sequence.list[idx++] = node->sequence.list[i];
            }
        }
        node->sequence.length = idx;
    } else if (node->tag == NODE_ALTERNATE) {
        for (int i = 0; i < node->sequence.length; i++)
            compress_literals(node->sequence.list[i]);
    } else if (node->tag == NODE_REPEAT) {
        compress_literals(node->repeat.el);
    }

}

regex_node_t *collapse_alts(regex_node_t *node) {
    // blah
    return NULL;
}

int create_label(vm_program_t *prog, int offset) {
    int label = prog->current_label;
    prog->current_label++;

    prog->label_table[label] = prog->insts_length + offset;

    return label;
}

int add_inst(vm_program_t *prog, vm_inst_t inst) {
    if (prog->insts_length == prog->insts_capacity) exit(-1);

    int index = prog->insts_length;
    prog->insts[index] = inst;
    prog->insts_length++;

    return index;
}

void emit_node(vm_program_t *prog, regex_node_t *node) {
    vm_inst_t inst;

    if (node->tag == NODE_LITERAL) {
        inst.op = OP_LITERAL;
        inst.literal.str    = node->literal.str;
        inst.literal.length = node->literal.length;
        add_inst(prog, inst);

    } else if (node->tag == NODE_ANY) {
        inst.op = OP_ANY;
        add_inst(prog, inst);

    } else if (node->tag == NODE_SEQUENCE) {
        for (int i = 0; i < node->sequence.length; i++)
            emit_node(prog, node->sequence.list[i]);

    } else if (node->tag == NODE_ALTERNATE) {
        inst.op = OP_SPLIT;
        int split_idx = add_inst(prog, inst);

        int alt1_label = create_label(prog, 0);
        emit_node(prog, node->sequence.list[0]);

        inst.op = OP_JMP;
        int jmp_idx = add_inst(prog, inst);

        int alt2_label = create_label(prog, 0);
        emit_node(prog, node->sequence.list[1]);

        // fix up labels
        int jmp_label = create_label(prog, 0);
        prog->insts[jmp_idx].jmp_label = jmp_label;

        prog->insts[split_idx].split.label_1 = alt1_label;
        prog->insts[split_idx].split.label_2 = alt2_label;

    } else if (node->tag == NODE_REPEAT) {
        if (node->repeat.min == 0 && node->repeat.max == 1) { // '?'
            inst.op = OP_SPLIT;
            int split_idx = add_inst(prog, inst);

            int lab1 = create_label(prog, 0);
            emit_node(prog, node->repeat.el);

            int lab2 = create_label(prog, 0);

            // fix up labels
            prog->insts[split_idx].split.label_1 = lab1;
            prog->insts[split_idx].split.label_2 = lab2;

        } else if (node->repeat.min == 0 && node->repeat.max == -1) { // '*'
            int L1 = create_label(prog, 0);
            inst.op = OP_SPLIT;
            int split_idx = add_inst(prog, inst);

            int L2 = create_label(prog, 0);
            emit_node(prog, node->repeat.el);

            inst.op = OP_JMP;
            inst.jmp_label = L1;
            add_inst(prog, inst);

            int L3 = create_label(prog, 0);
    
            // fix up labels
            prog->insts[split_idx].split.label_1 = L2;
            prog->insts[split_idx].split.label_2 = L3;

        } else if (node->repeat.min == 1 && node->repeat.max == -1) {
            int L1 = create_label(prog, 0);
            emit_node(prog, node->repeat.el);

            inst.op = OP_SPLIT;
            int split_idx = add_inst(prog, inst);

            int L3 = create_label(prog, 0);

            // fix up labels
            prog->insts[split_idx].split.label_1 = L1;
            prog->insts[split_idx].split.label_2 = L3;
        } else {
            printf("unsupported repetition\n");
            exit(-1);
        }
    }
}

vm_program_t *regex_compile_bytecode(const char *pattern) {
    const char *input = pattern;
    regex_node_t *node = regex_parse(&input);

    vm_program_t *prog = (vm_program_t*) malloc(sizeof(vm_program_t));
    prog->insts_capacity = 1000;
    prog->insts_length = 0;
    prog->insts = (vm_inst_t*) malloc(prog->insts_capacity * sizeof(vm_inst_t));

    prog->label_table = (int*) malloc(prog->insts_capacity * sizeof(int));
    prog->current_label = 0;

    emit_node(prog, node);
    // terminate with a match inst
    add_inst(prog, (vm_inst_t){.op = OP_MATCH});
    
    return prog;
}

match_fn_t regex_compile_jit(vm_program_t *prog) {
    arm_program_t arm;
    arm.index = 0;

    arm.f = fopen("asm/foo.s", "w");
    vm2arm(prog, &arm);
    fclose(arm.f);

    // assemble the code
    system("clang asm/foo.s -c -o asm/foo.o");
    // dump the code
    system("otool -tX asm/foo.o > asm/foo.txt");

    FILE *text = fopen("asm/foo.txt", "r");

    uint32_t *data = (uint32_t*) executable_mem(4096);
    pthread_jit_write_protect_np(false);
    sys_icache_invalidate(data, 4096);

    int addr = 0;
    uint64_t _unused;
    uint32_t b1, b2, b3, b4;
    int nret;
    while ((nret = fscanf(text, "%llx %x %x %x %x\n", &_unused, &b1, &b2, &b3, &b4)) > 1) {
        if (nret >= 2) data[addr++] = b1;
        if (nret >= 3) data[addr++] = b2;
        if (nret >= 4) data[addr++] = b3;
        if (nret >= 5) data[addr++] = b4;
    }
    fclose(text);

    pthread_jit_write_protect_np(true);
    sys_icache_invalidate(data, 4096);

    match_fn_t fn = (match_fn_t) data;
    return fn;
}

match_fn_t regex_compile(const char *pattern) {
    vm_program_t *prog = regex_compile_bytecode(pattern);
    match_fn_t fn = regex_compile_jit(prog);
    return fn;
}

void test(const char *pattern) {
    printf("Test pattern: %s\n", pattern);
    
    const char *pat = pattern;

    regex_node_t *node = regex_parse(&pat);
    node = eliminate_single_seqs(node);
    compress_literals(node);

    printf(" > Reconstructed: ");
    print_node(node);
    printf("\n");
    print_node_tree(node, 0);
}

#include <time.h>

void benchmark() {
    const char *pattern = "(hello|world(0|1|2|3)?)+";

    int len = 50 * 1000 * 1024;
    char *str = (char*) malloc(len);
    memset(str, '\0', len);

    int ctr = 0;
    for (int i = 0; i < len-100; ) {
        ctr++;
        if ((ctr & 1) == 1) {
            memcpy(str + i, "hello", 5);
            i += 5;
        } else {
            memcpy(str + i, "world", 5);
            str[i+5] = '0' + (ctr/2 % 4);
            i += 6;
        }
    }

    printf("the string: %.*s\n", 100, str);

    vm_program_t *prog = regex_compile_bytecode(pattern);

    double total = 0;
    int niters = 20;
    for (int iter = 0; iter < niters; iter++) {
        double start = (double) clock() / CLOCKS_PER_SEC;
        vm_run(prog, str);
        double end = (double) clock() / CLOCKS_PER_SEC;

        total += (end - start);
    }

    printf("Total %f, avg %f\n", total, total / niters);

    printf("result ::: %d\n", vm_run(prog, str));

    match_fn_t fn = regex_compile(pattern);
    printf("result ::: %d\n", fn(str));

    total = 0;
    for (int iter = 0; iter < niters; iter++) {
        double start = (double) clock() / CLOCKS_PER_SEC;
        fn(str);
        double end = (double) clock() / CLOCKS_PER_SEC;

        total += (end - start);
    }

    printf("Total %f, avg %f\n", total, total / niters);

    re2::RE2 re(pattern);

    total = 0;
    for (int iter = 0; iter < niters; iter++) {
        double start = (double) clock() / CLOCKS_PER_SEC;
        re2::RE2::FullMatch(str, re);
        double end = (double) clock() / CLOCKS_PER_SEC;

        total += (end - start);
    }

    printf("Total %f, avg %f\n", total, total / niters);


}

int main(int argc, char **argv) {
    test("");
    test("123");
    test("1(2)3");
    test("a|b");
    test("a.c");
    test("1(2|3)4");
    test("1(2|)4");
    test("1|2|3|4");
    test("1+");
    test("1?");
    test("1*(124)+");
    test("123(abcd+)");
    test("(hello(xyz)world)");

    const char *pattern = "(hello|world(0|1|2|3)?)+";
    match_fn_t fn = regex_compile(pattern);

    const char *pp = "hellohellohelloworld3";
    bool answer = fn(pp);
    printf("drumroll... %d\n", answer);

    vm_program_t *prog = regex_compile_bytecode(pattern);
    //print_program(prog);

    bool vm_ans = vm_run(prog, pp);
    printf("vm ans: %d\n", vm_ans);

    benchmark();

    return 0;
}
