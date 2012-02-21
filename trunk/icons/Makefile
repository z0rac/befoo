#
# Copyright (C) 2010-2012 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
#
# This software comes with ABSOLUTELY NO WARRANTY; for details of
# the license terms, see the LICENSE.txt file included with the program.
#
TARGETS = simple.ico

BINPREFIX = i686-pc-mingw32-
LDFLAGS = -mwindows -mdll --no-export-all-symbols
LDFLAGS += -Wl,--strip-all -Wl,-O1 -Wl,--as-needed

.SUFFIXES: .rc .res .ico
.rc.res:	; $(BINPREFIX)windres -O coff -i $< -o $@
.res.ico:	; $(BINPREFIX)dllwrap $(LDFLAGS) -o $@ $<

all: $(TARGETS)

$(TARGETS): %.ico: $(wildcard %/*.ico)

clean: mostlyclean	; @$(RM) $(TARGETS)
distclean: clean	; @$(RM) *~
mostlyclean:		; @$(RM) $(TARGETS:.ico=.res)
