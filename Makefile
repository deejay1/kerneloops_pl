
CFLAGS := -O2 -g -fstack-protector-all -D_FORTIFY_SOURCE=2 -Wall -W -Wstrict-prototypes -Wundef -fno-common -Werror-implicit-function-declaration -Wdeclaration-after-statement 
CFLAGS += `pkg-config --cflags libnotify gtk+-2.0`
LDF_A := `pkg-config --libs libnotify gtk+-2.0`
LDF_D := `pkg-config --libs glib-2.0 dbus-glib-1` `curl-config --libs` -Wl,"-z relro" -Wl,"-z now" 

all:	kerneloops kerneloops-applet


kerneloops:	kerneloops.o submit.o dmesg.o configfile.o kerneloops.h
	gcc kerneloops.o submit.o dmesg.o configfile.o $(LDF_D) -o kerneloops

kerneloops-applet: kerneloops-applet.o
	gcc kerneloops-applet.o $(LDF_A)-o kerneloops-applet

clean:
	rm -f *~ *.o *.ko DEADJOE kerneloops kernel-oops-applet *.out */*~

dist: clean
	rm -rf .git .gitignore push.sh .*~  */*~


install:
	mkdir -p $(DESTDIR)/usr/sbin/ $(DESTDIR)/etc/xdg/autostart
	mkdir -p $(DESTDIR)/usr/share/kerneloops $(DESTDIR)/etc/dbus-1/system.d/
	install -m 0755 kerneloops $(DESTDIR)/usr/sbin
	install -m 0755 kerneloops-applet $(DESTDIR)/usr/bin
	install -m 0644 kerneloops.conf $(DESTDIR)/etc/kerneloops.conf
	install -m 0755 kerneloops-applet.desktop $(DESTDIR)/etc/xdg/autostart/
	install -m 0755 kerneloops.dbus $(DESTDIR)/etc/dbus-1/system.d/
	install -m 0755 icon.png $(DESTDIR)/usr/share/kerneloops/icon.png

tests: kerneloops
	for i in test/*txt ; do echo -n . ; ./kerneloops --debug $$i > $$i.dbg ; diff -u $$i.out $$i.dbg ; done ; echo
	[ -e /usr/bin/valgrind ] && for i in test/*txt ; do echo -n . ; valgrind -q ./kerneloops --debug $$i > $$i.dbg ; diff -u $$i.out $$i.dbg ; done ; echo

valgrind: kerneloops tests
	valgrind -q --leak-check=full ./kerneloops --debug test/*.txt
	for i in test/*txt ; do valgrind -q ./kerneloops --debug $$i > $$i.dbg ; diff -u $$i.out $$i.dbg ; done
