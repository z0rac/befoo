#
# Copyright (C) 2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
#
# This software comes with ABSOLUTELY NO WARRANTY; for details of
# the license terms, see the LICENSE.txt file included with the program.
#
TARGETS = simple.ico
MAKE := $(MAKE) -f../Makefile.sub

all: $(TARGETS)

$(TARGETS): %.ico: %/icon.rc $(wildcard %/*.ico)

$(TARGETS):		; $(MAKE) -C $(basename $@)
%.clean:		; $(MAKE) -C $(basename $@) clean
%.distclean:		; $(MAKE) -C $(basename $@) distclean

clean: mostlyclean $(TARGETS:.ico=.clean)
distclean: mostlyclean $(TARGETS:.ico=.distclean) ; @$(RM) *~
mostlyclean:		; @$(RM) $(TARGETS)
