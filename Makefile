
CFLAGS := -O2 -g -fstack-protector -D_FORTIFY_SOURCE=2 -Wall -W 


kerneloops:	kerneloops.o submit.o dmesg.o configfile.o kerneloops.h
	gcc kerneloops.o submit.o dmesg.o configfile.o `curl-config --libs` -Wl,"-z relro" -Wl,"-z now" -o kerneloops

clean:
	rm -f *~ *.o *.ko DEADJOE kerneloops *.out

dist: clean
	rm -rf .git .gitignore push.sh .*~ 


install:
	mkdir -p $(DESTDIR)/usr/sbin $(DESTDIR)/etc
	cp kerneloops $(DESTDIR)/usr/sbin
	cp kerneloops.org $(DESTDIR)/etc
	
tests: kerneloops
	for i in test/*txt ; do ./kerneloops --debug $$i > $$i.dbg ; diff -u $$i.out $$i.dbg ; done