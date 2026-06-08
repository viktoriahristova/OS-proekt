CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11 -pthread

all: hangman-server hangman-client

hangman-server: hangman-server.c game.c game.h
	$(CC) $(CFLAGS) hangman-server.c game.c -o hangman-server

hangman-client: hangman-client.c
	$(CC) $(CFLAGS) hangman-client.c -o hangman-client

clean:
	rm -f hangman-server hangman-client
