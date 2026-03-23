CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Werror

all: aesdsocketserver

default: all

clean:
	rm -f *.o aesdsocket

aesdsocketserver: aesdsocket.c
	$(CC) $(CFLAGS) -c aesdsocket.c -o aesdsocket.o
	$(CC) $(CFLAGS) aesdsocket.o -o aesdsocket