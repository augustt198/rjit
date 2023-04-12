#include "rjit.h"

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

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

// remove sequence nodes with a single child
regex_node_t *eliminate_single_seqs(regex_node_t *node) {
    if (node->tag == NODE_SEQUENCE && node->sequence.length == 1) {
        regex_node_t *ret = node->sequence.list[0];
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

regex_node_t *collapse_alts(regex_node_t *node) {
    // blah
    return NULL;
}

match_fn_t regex_compile(const char *pattern) {
    return NULL;
}

void test(const char *pattern) {
    printf("Test pattern: %s\n", pattern);
    
    const char *pat = pattern;

    regex_node_t *node = regex_parse(&pat);
    node = eliminate_single_seqs(node);

    printf(" > Reconstructed: ");
    print_node(node);
    printf("\n");
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

    return 0;
}