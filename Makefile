CC=gcc
CFLAGS=-Wall -g -std=c99 -pthread
LDFLAGS=-lm

all: master view player

master: main_master.c
	$(CC) $(CFLAGS) main_master.c -o master $(LDFLAGS)

view: view.c
	$(CC) $(CFLAGS) view.c -o view $(LDFLAGS)

player: player.c
	$(CC) $(CFLAGS) player.c -o player $(LDFLAGS)

clean:
	rm -f master view player

