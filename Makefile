CC=gcc
CFLAGS=-Wall -g -std=c99 -pthread
BINDIR=bin

all: $(BINDIR) master view player

$(BINDIR):
    mkdir -p $(BINDIR)

master: main_master.c
    $(CC) $(CFLAGS) main_master.c -o $(BINDIR)/master

view: view.c
    $(CC) $(CFLAGS) view.c -o $(BINDIR)/view

player: player.c
    $(CC) $(CFLAGS) player.c -o $(BINDIR)/player

clean:
    rm -rf $(BINDIR)

