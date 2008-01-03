
#
# to build this package, you need to have the following components installed:
# dbus-glib-devel libnotify-devel gtk2-devel curl-devel
#

BINDIR=/usr/bin
LOCALESDIR=/usr/share/locale
MANDIR=/usr/share/man/man8
CC?=gcc

CFLAGS := -O2 -g -fstack-protector -D_FORTIFY_SOURCE=2 -Wall -W -Wstrict-prototypes -Wundef -fno-common -Werror-implicit-function-declaration -Wdeclaration-after-statement

MY_CFLAGS := `pkg-config --cflags libnotify gtk+-2.0`
#
# pkg-config tends to make programs pull in a ton of libraries, not all 
# are needed. -Wl,--as-needed tells the linker to just drop unused ones,
# and that makes the applet load faster and use less memory.
#
LDF_A := -Wl,--as-needed `pkg-config --libs libnotify gtk+-2.0`
LDF_D := -Wl,--as-needed `pkg-config --libs glib-2.0 dbus-glib-1` `curl-config --libs` -Wl,"-z relro" -Wl,"-z now" 

all:	kerneloops kerneloops-applet kerneloops.1.gz

.c.o:
	$(CC) $(CFLAGS) $(MY_CFLAGS) -c -o $@ $<
 

kerneloops:	kerneloops.o submit.o dmesg.o configfile.o kerneloops.h
	gcc kerneloops.o submit.o dmesg.o configfile.o $(LDF_D) -o kerneloops
	@(cd po/ && $(MAKE))

kerneloops-applet: kerneloops-applet.o
	gcc kerneloops-applet.o $(LDF_A)-o kerneloops-applet

kerneloops.1.gz: kerneloops.1
	gzip -9 -c $< > $@

clean:
	rm -f *~ *.o *.ko DEADJOE kerneloops kerneloops-applet *.out */*~ kerneloops.1.gz
	@(cd po/ && $(MAKE) $@)

dist: clean
	rm -rf .git .gitignore push.sh .*~  */*~


install: kerneloops kerneloops-applet kerneloops.1.gz
	mkdir -p $(DESTDIR)/usr/sbin/ $(DESTDIR)/etc/xdg/autostart
	mkdir -p $(DESTDIR)/usr/share/kerneloops $(DESTDIR)/etc/dbus-1/system.d/
	mkdir -p $(DESTDIR)$(BINDIR) $(DESTDIR)$(MANDIR)
	install -m 0755 kerneloops $(DESTDIR)/usr/sbin
	install -m 0755 kerneloops-applet $(DESTDIR)$(BINDIR)
	install -m 0644 kerneloops.conf $(DESTDIR)/etc/kerneloops.conf
	desktop-file-install -m 0644 --vendor="kerneloops.org" --dir=$(DESTDIR)/etc/xdg/autostart/ kerneloops-applet.desktop
	install -m 0755 kerneloops.dbus $(DESTDIR)/etc/dbus-1/system.d/
	install -m 0755 kerneloops.1.gz $(DESTDIR)$(MANDIR)
	install -m 0755 icon.png $(DESTDIR)/usr/share/kerneloops/icon.png
	@(cd po/ && env LOCALESDIR=$(LOCALESDIR) DESTDIR=$(DESTDIR) $(MAKE) $@)
	
	
# This is for translators. To update your po with new strings, do :
# svn up ; make uptrans LG=fr # or de, ru, hu, it, ...
uptrans:
	xgettext -C -s -k_ -o po/kerneloops.pot *.c *.h
	@(cd po/ && env LG=$(LG) $(MAKE) $@)

	

tests: kerneloops
	desktop-file-validate *.desktop
	for i in test/*txt ; do echo -n . ; ./kerneloops --debug $$i > $$i.dbg ; diff -u $$i.out $$i.dbg ; done ; echo
	[ -e /usr/bin/valgrind ] && for i in test/*txt ; do echo -n . ; valgrind -q --leak-check=full ./kerneloops --debug $$i > $$i.dbg ; diff -u $$i.out $$i.dbg ; done ; echo

valgrind: kerneloops tests
	valgrind -q --leak-check=full ./kerneloops --debug test/*.txt


