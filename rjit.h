#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>

typedef bool (*match_fn_t)(const char *str);

typedef enum {
    NODE_NULL, // i.e. a dummy node
    NODE_LITERAL,
    NODE_SPECIAL_LITERAL,
    NODE_CHAR_CLASS,
    NODE_SEQUENCE,
    NODE_ALTERNATE,
    NODE_REPEAT, // i.e. ?, +, *, {...}
    NODE_ANY
} regex_node_tag_t;

typedef enum {
    META_WORD_CHAR,
    META_NON_WORD_CHAR,
    META_DIGIT_CHAR,
    META_NON_DIGIT_CHAR,
    META_WHITESPACE_CHAR,
    META_NON_WHITESPACE_CHAR,
} regex_special_literal_t;

typedef struct regex_node_t {
    regex_node_tag_t tag;
    // used for convenience during parsing
    struct regex_node_t *next;

    union {
        struct {
            const char *str;
            int length;
        } literal;

        struct {
            bool invert;
            const char *char_starts;
            const char *char_ends;
            int length;
        } char_class;

        // for both NODE_SEQUENCE and NODE_ALTERNATE
        struct {
            struct regex_node_t **list;
            int length;
        } sequence;

        struct {
            struct regex_node_t *el;
            int min;
            int max;
        } repeat;
    };
} regex_node_t;

typedef enum {
    OP_LITERAL,
    OP_ANY,
    OP_JMP,
    OP_SPLIT,
    OP_MATCH
} vm_opcode_t;

typedef struct {
    vm_opcode_t op;

    union {
        struct {
            const char *str;
            int length;
        } literal;

        int jmp_label;

        struct {
            int label_1;
            int label_2;
        } split;
    };
} vm_inst_t;

typedef struct {
    vm_inst_t *insts;
    int insts_length;
    int insts_capacity;

    int *label_table;
    int current_label;
} vm_program_t;

int create_label(vm_program_t *prog, int offset);

int add_inst(vm_program_t *prog, vm_inst_t inst);

void print_node(regex_node_t *node);
void print_node_tree(regex_node_t *node, int level);
void print_program(vm_program_t *prog);

void *executable_mem(int size);


typedef int reg_t;
typedef uint32_t arm_inst_t;

typedef struct {
    arm_inst_t *insts;
    int index;

    FILE *f;

    int *label_table;
} arm_program_t;

void vm2arm(vm_program_t *vp, arm_program_t *ap);
