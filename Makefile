## Explicitely define CFLAGS to avoid function pointer to void* conversion warnings
CFLAGS = -std=c99 -D_XOPEN_SOURCE=700 -g -D__DEBUG__ -Wall -Wextra -Wno-unused-value
OBJS = runtime.o
HEADERS = runtime.h
.PHONY: clean test

all: schemel

clean:
	rm -f $(OBJS)

schemel: main.c $(OBJS) $(HEADERS)
	gcc -g -I. -o schemel main.c runtime.o -lgmp

test: schemel
	./schemel test/001.scm && test "$$(./test/001)" = "230" && echo 001 OK
	./schemel test/002.scm && test "$$(./test/002)" = "2"   && echo 002 OK
	./schemel test/003.scm && test "$$(./test/003)" = "10"  && echo 003 OK
	./schemel test/004.scm && test "$$(./test/004)" = "22"  && echo 004 OK
	./schemel test/005.scm && test "$$(./test/005)" = "30414093201713378043612608166064768844377641568960512000000000000" && echo 005 OK
	./schemel test/006.scm && test "$$(./test/006)" = "20"  && echo 006 OK
	./schemel test/007.scm && test "$$(./test/007)" = "(1 2)"    && echo 007 OK
	./schemel test/008.scm && test "$$(./test/008)" = "(3 0 3)"  && echo 008 OK
	./schemel test/009.scm && test "$$(./test/009)" = "((1 5) (2 6) (3 7) (4 8))"  && echo 009 OK
	./schemel test/010.scm && test "$$(./test/010)" = "#f"  && echo 010 OK
	./schemel test/011.scm && test "$$(./test/011)" = "5"   && echo 011 OK
	./schemel test/013.scm && test "$$(./test/013)" = "5"   && echo 013 OK
