CFLAGS  =-W -Wall -std=c99
LDFLAGS =-lX11 -lXext -lXtst

all: kbosd

.PHONY: clean

clean:
	rm -f *.o
