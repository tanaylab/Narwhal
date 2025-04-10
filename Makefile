.PHONY: all format run_test

CC = gcc

all: format run_test

run_test: test
	./test run

test: test.c narwhal.c narwhal.h
	cc -o test test.c narwhal.c

format:
	clang-format -i *.h *.c

clean:
	rm -rf test tmp.*
