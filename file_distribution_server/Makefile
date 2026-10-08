CC     = gcc
CFLAGS = -Wall -Wextra -g -O2 -pthread
LDLIBS = -pthread

all: server client

server: main.c
	$(CC) $(CFLAGS) main.c -o server $(LDLIBS)

client: client.c
	$(CC) $(CFLAGS) client.c -o client $(LDLIBS)

# Build with AddressSanitizer to catch memory bugs while testing
debug: CFLAGS += -fsanitize=address,undefined
debug: LDLIBS += -fsanitize=address,undefined
debug: clean all

clean:
	rm -f server client

.PHONY: all debug clean
