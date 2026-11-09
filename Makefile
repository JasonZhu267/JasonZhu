CC = clang
CFLAGS = -Wall -Wextra -std=c99

all: server client

server: server.c
	$(CC) $(CFLAGS) server.c lossy.c -o server

client: client.c
	$(CC) $(CFLAGS) client.c lossy.c -o client

clean:
	rm -f server client out.txt *.o received_*

.PHONY: all clean
