#
# Copyright (C) 2009-2013 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
#
# This software comes with ABSOLUTELY NO WARRANTY; for details of
# the license terms, see the LICENSE.txt file included with the program.
#
NAME = extend

ifeq ($(x64), 1)
BINPREFIX = x86_64-w64-mingw32-
else
BINPREFIX = i686-w64-mingw32-
endif
CC = $(BINPREFIX)gcc
CXX = $(BINPREFIX)g++

OPTS =
LIBDIR = ../src
CFLAGS = $(OPTS) -D_USRDLL -DISOLATION_AWARE_ENABLED=1 -I$(LIBDIR)
LDFLAGS = $(OPTS) -mwindows -mdll --no-export-all-symbols -static

ifdef USE_REG # use registory instead of .INI file.
CFLAGS += -DUSE_REG=$(USE_REG)
endif

ifdef debug
CFLAGS += -D_DEBUG=$(debug) -g -Wall
else
CFLAGS += -DNDEBUG -Os
LDFLAGS += -Wl,--strip-all -Wl,-O1 -Wl,--as-needed
endif
CXXFLAGS = $(CFLAGS)

OBJS = main.o settingdlg.o mailboxdlg.o icondlg.o $(LOBJS) $(NAME).res
LOBJS = setting.o uri.o icon.o win32.o
LIBS = -lstdc++ -lshlwapi -lcomctl32 -lwinmm -lole32 -luuid

RSRCS = en.rc ja.rc

all: $(NAME).dll

$(NAME).dll: $(OBJS)
	$(BINPREFIX)dllwrap $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(LOBJS): %.o: $(LIBDIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

$(NAME).res: $(RSRCS)
	cat $(RSRCS) | $(BINPREFIX)windres -I$(LIBDIR) -O coff -o $@

$(OBJS): Makefile
main.o settingdlg.o mailboxdlg.o icondlg.o: settingdlg.h define.h
main.o settingdlg.o mailboxdlg.o icondlg.o: $(LIBDIR)/win32.h $(LIBDIR)/setting.h
settingdlg.o icondlg.o: $(LIBDIR)/icon.h
mailboxdlg.o uri.o: $(LIBDIR)/mailbox.h
setting.o icon.o: $(LIBDIR)/win32.h
setting.o icon.o win32.o: %.o: $(LIBDIR)/%.h
$(NAME).res: define.h $(LIBDIR)/version.h

.PHONY: clean distclean mostlyclean
clean: mostlyclean	; @$(RM) $(NAME).dll
distclean: clean	; @$(RM) *~
mostlyclean:		; @$(RM) $(OBJS)
