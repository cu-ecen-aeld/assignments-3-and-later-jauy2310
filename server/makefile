CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Werror
TARGET ?= aesdsocket
LDFLAGS ?= -lpthread -lrt -Wl --hash-style=gnu

all: aesdsocket

${TARGET}: ${TARGET}.o
	$(CC) ${TARGET}.o -o ${TARGET} $(CFLAGS) ${LDFLAGS}

${TARGET}.o: ${TARGET}.c ${TARGET}.h
	$(CC) -c ${TARGET}.c -o ${TARGET}.o $(CFLAGS) ${LDFLAGS}

clean:
	rm -f *.o ${TARGET}
