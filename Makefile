all:
	clang -Wall -ggdb3 rjit.c util.c vm2arm.c -o rjit
