#
# Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
#
# This software comes with ABSOLUTELY NO WARRANTY; for details of
# the license terms, see the LICENSE.txt file included with the program.
#
TARGETS = befoo.exe extend.dll
SUBDIRS = src extend icons

all: $(TARGETS)

befoo.exe: src		; @cp $</$@ .
extend.dll: extend	; @cp $</$@ .
icon: icons		; @/usr/bin/find $< -maxdepth 1 -name '*.ico' -exec cp {} . \;

$(SUBDIRS)::		; @$(MAKE) -C $@
%.clean:		; @$(MAKE) -C $(basename $@) clean
%.distclean:		; @$(MAKE) -C $(basename $@) distclean

clean: mostlyclean $(SUBDIRS:=.clean)
distclean: mostlyclean $(SUBDIRS:=.distclean) ; @$(RM) *~
mostlyclean: 		; @$(RM) befoo.exe extend.dll *.ico
