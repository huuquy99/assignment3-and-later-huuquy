CC = $(CROSS_COMPILE)gcc

all: aesdsocketserver

default: all

clean:
	rm -f *.o aesdsocket

aesdsocketserver: aesdsocket.c
	$(CC) -c aesdsocket.c -o aesdsocket.o
	$(CC) aesdsocket.o -o aesdsocket