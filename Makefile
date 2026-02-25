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

$(SERVER): server.c
	$(CC) $(CFLAGS) -o $@ $<

$(CLIENT): client.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(SERVER) $(CLIENT)