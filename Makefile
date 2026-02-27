.PHONY: all clean

MAKEFLAGS += --no-print-directory --silent

CC      = gcc
CFLAGS  = -Wall -Werror -Wextra -Wpedantic

SERVER  = server
CLIENT  = client

all:
	clear
	@$(MAKE) -q $(SERVER) && echo "'server' is up to date." || $(MAKE) $(SERVER)
	@$(MAKE) -q $(CLIENT) && echo "'client' is up to date." || $(MAKE) $(CLIENT)

$(SERVER): server.c helpers.c
	$(CC) $(CFLAGS) -o $@ server.c helpers.c

$(CLIENT): client.c helpers.c
	$(CC) $(CFLAGS) -o $@ client.c helpers.c

clean:
	rm -f $(SERVER) $(CLIENT)