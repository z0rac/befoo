#
# Copyright (C) 2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
#
# This software comes with ABSOLUTELY NO WARRANTY; for details of
# the license terms, see the LICENSE.txt file included with the program.
#
TARGETS = simple.ico
LDFLAGS = -mno-cygwin --target=i386-mingw32 --driver-name=gcc-3
LDFLAGS += -Wl,--strip-all -Wl,--as-needed

.SUFFIXES: .rc .res .ico
.rc.res:	; windres -O coff -i $< -o $@
.res.ico:	; dllwrap $(LDFLAGS) -o $@ $<

all: $(TARGETS)

$(TARGETS): %.ico: $(wildcard %/*.ico)

clean: mostlyclean	; @$(RM) $(TARGETS)
distclean: clean	; @$(RM) *~
mostlyclean:		; @$(RM) $(TARGETS:.ico=.res)
