CC=gcc
CFLAGS=-I.
DEPS = mem.h

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

production: mem.c mem_rand.c
	$(CC) -c -fpic mem.c -Wall -Werror -I.
	$(CC)	-shared -o libmem.so mem.o
	$(CC) -L. -lmem -o mem_rand mem_rand.c -Wall -Werror

dev: mem.c mem_rand.c
	$(CC) -c -ggdb -fpic mem.c -Wall -Werror -I.
	$(CC)	-shared -o libmem.so mem.o
	$(CC) -o mem_rand mem_rand.c mem.o -Wall -Werror


test: generate
	./test.sh
