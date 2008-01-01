#
# Request to translators: Please don't send me patches agaist .po files;
# experience has shown that this gets too mangled by email clients due
# to UTF-8 and such. What works best is just attaching the full .po file
# to an email.
#



SRC=$(wildcard *.po)
OBJ= $(SRC:.po=.mo)
INST= $(SRC:.po=.inst)

translations: $(OBJ)
	
%.mo: %.po
	-@msgfmt -o $@  $<

clean:
	rm -f *.mo *~

# hack to automate installation dynamicaly, without previous knowledge of 
# the po/mo file list (we fool make by pretending the need for .inst files).
install: $(OBJ) $(INST)

%.inst: %.mo
	mkdir -p $(DESTDIR)$(LOCALESDIR)/$*/LC_MESSAGES/
	-cp -f $< $(DESTDIR)$(LOCALESDIR)/$*/LC_MESSAGES/kerneloops.mo

uptrans: $(LG).po

$(LG).po: kerneloops.pot
ifdef LG
	msgmerge -U $@ $<
else
	@echo "Usage : make uptrans LG=xx # with xx = de, es, fi ..."
	@exit 1
endif

