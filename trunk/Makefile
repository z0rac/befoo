#
# Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
#
# This software comes with ABSOLUTELY NO WARRANTY; for details of
# the license terms, see the LICENSE.txt file included with the program.
#
NAME = befoo
SRCDIR = src
EXTDIR = extend
TARGETS = $(NAME).exe extend.dll

all: $(TARGETS)

$(NAME).exe:
	@$(MAKE) -C $(SRCDIR)
	@mv $(SRCDIR)/$@ .

extend.dll:
	@$(MAKE) -C $(EXTDIR)
	@mv $(EXTDIR)/$@ .

clean: mostlyclean
	@$(MAKE) -C $(SRCDIR) $@
	@$(MAKE) -C $(EXTDIR) $@

distclean: mostlyclean
	@$(RM) *~
	@$(MAKE) -C $(SRCDIR) $@
	@$(MAKE) -C $(EXTDIR) $@

mostlyclean:
	@$(RM) $(TARGETS)
