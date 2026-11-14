PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

all: hola stackframe

hola: hola.c
	gcc -O0 -g -o hola hola.c

stackframe: stackframe.c
	gcc -O0 -g -fno-omit-frame-pointer -o stackframe stackframe.c

install: all
	install -d $(BINDIR)
	install -m 755 hola $(BINDIR)/hola-chayanne
	install -m 755 stackframe $(BINDIR)/stackframe-chayanne

clean:
	rm -f hola stackframe
