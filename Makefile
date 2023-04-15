all:
	clang++ --std=c++11 -Wall -ggdb3 rjit.c util.c vm2arm.c vmsim.c -lre2 -o rjit
