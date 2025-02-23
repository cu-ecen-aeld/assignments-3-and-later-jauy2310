CC:=$(CROSS_COMPILE)gcc
CFLAGS+=-g -Wall -Werror

all: aesdsocket

aesdsocket: aesdsocket.o
	$(CC) aesdsocket.o -o aesdsocket $(CFLAGS)

aesdsocket.o: aesdsocket.c aesdsocket.h
	$(CC) -c aesdsocket.c -o aesdsocket.o $(CFLAGS)

clean:
	rm -f *.o aesdsocket
