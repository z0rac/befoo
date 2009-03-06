#
# Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
#
# This software comes with ABSOLUTELY NO WARRANTY; for details of
# the license terms, see the LICENSE.txt file included with the program.
#
NAME = befoo
SRCDIR = src
EXTDIR = extend

all:	$(NAME).exe extend.dll

$(NAME).exe:
	$(MAKE) -C $(SRCDIR)
	mv $(SRCDIR)/$(NAME).exe .

extend.dll:
	$(MAKE) -C $(EXTDIR)
	mv $(EXTDIR)/extend.dll .

clean:
	$(MAKE) -C $(SRCDIR) clean
	$(MAKE) -C $(EXTDIR) clean
