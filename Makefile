BIN = lith
CC = gcc
CFLAGS = -g -std=c89 -Wall
LDFLAGS = 

SRCS = lith.c main.c
OBJS = $(SRCS:.c=.o)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c lith.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(BIN) $(OBJS)

all: $(BIN)
