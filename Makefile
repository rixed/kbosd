CFLAGS  =-W -Wall -std=c99
LDFLAGS =-lX11 -lXext -lXtst

all: kbosd

kbosd: kbosd.o topmost.o

.PHONY: clean

clean:
	rm -f *.o

PREFIX := /usr/local/

install: kbosd kbosd.1
	install -d $(DESTDIR)$(PREFIX)bin/ $(DESTDIR)$(PREFIX)share/man/man1/ $(DESTDIR)$(PREFIX)share/kbosd/
	install ./kbosd $(DESTDIR)$(PREFIX)bin/
	install -m644 ./kbosd.1 $(DESTDIR)$(PREFIX)share/man/man1/
	install -m644 ./default.layout $(DESTDIR)$(PREFIX)share/kbosd/
	install -m644 ./french.layout $(DESTDIR)$(PREFIX)share/kbosd/
