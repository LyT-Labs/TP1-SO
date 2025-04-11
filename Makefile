CC=gcc
CFLAGS=-Wall -g -std=c99 -pthread

all: master view player

master: main_master.c
	$(CC) $(CFLAGS) main_master.c -o master

view: view.c
	$(CC) $(CFLAGS) view.c -o view

player: player.c
	$(CC) $(CFLAGS) player.c -o player

clean:
	rm -f master view player

