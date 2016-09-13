#
# Copyright (C) 2010-2016 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
#
# This software comes with ABSOLUTELY NO WARRANTY; for details of
# the license terms, see the LICENSE.txt file included with the program.
#
TARGETS = simple.ico

LDFLAGS = -mwindows -mdll --no-export-all-symbols
LDFLAGS += -Wl,--strip-all -Wl,-O1 -Wl,--as-needed

.SUFFIXES: .rc .res .ico
.rc.res:	; windres -O coff -i $< -o $@
.res.ico:	; dllwrap $(LDFLAGS) -o $@ $<

all: $(TARGETS)

$(TARGETS): %.ico: $(wildcard %/*.ico)

clean: mostlyclean	; @$(RM) $(TARGETS)
distclean: clean	; @$(RM) *~
mostlyclean:		; @$(RM) $(TARGETS:.ico=.res)
