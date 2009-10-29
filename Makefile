CFLAGS  =-W -Wall -std=c99
LDFLAGS =-lX11 -lXext -lXtst

all: kbosd

kbosd: kbosd.o topmost.o

.PHONY: clean

clean:
	rm -f *.o
