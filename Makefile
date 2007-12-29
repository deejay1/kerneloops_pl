
CFLAGS := -O2 -g -fstack-protector-all -D_FORTIFY_SOURCE=2 -Wall -W -Wstrict-prototypes -Wundef -fno-common -Werror-implicit-function-declaration -Wdeclaration-after-statement 


kerneloops:	kerneloops.o submit.o dmesg.o configfile.o kerneloops.h
	gcc kerneloops.o submit.o dmesg.o configfile.o `curl-config --libs` -Wl,"-z relro" -Wl,"-z now" -o kerneloops

clean:
	rm -f *~ *.o *.ko DEADJOE kerneloops *.out */*~

dist: clean
	rm -rf .git .gitignore push.sh .*~  */*~


install:
	mkdir -p $(DESTDIR)/usr/sbin/ $(DESTDIR)/etc
	install -m 0755 kerneloops $(DESTDIR)/usr/sbin
	install -m 0644 kerneloops.conf $(DESTDIR)/etc/kerneloops.conf

tests: kerneloops
	for i in test/*txt ; do echo -n . ; ./kerneloops --debug $$i > $$i.dbg ; diff -u $$i.out $$i.dbg ; done ; echo
	[ -e /usr/bin/valgrind ] && for i in test/*txt ; do echo -n . ; valgrind -q ./kerneloops --debug $$i > $$i.dbg ; diff -u $$i.out $$i.dbg ; done ; echo

valgrind: kerneloops tests
	valgrind -q --leak-check=full ./kerneloops --debug test/*.txt
	for i in test/*txt ; do valgrind -q ./kerneloops --debug $$i > $$i.dbg ; diff -u $$i.out $$i.dbg ; done
