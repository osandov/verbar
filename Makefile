include config.mk

OBJS := main.o \
	util.o \
	cpu.o \
	dropbox.o \
	mem.o \
	net.o \
	nics.o \
	power.o \
	volume.o \
	pa_watcher.o

verbar: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: install
install: verbar
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m755 verbar $(DESTDIR)$(PREFIX)/bin/

.PHONY: clean
clean:
	rm -f verbar $(OBJS)
