#
# Copyright (C) 2009-2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
#
# This software comes with ABSOLUTELY NO WARRANTY; for details of
# the license terms, see the LICENSE.txt file included with the program.
#
DLL = extend.dll

CC = gcc-3
CXX = g++-3

OPTS = -mwin32 -m32 -mno-cygwin
LIBDIR = ../src
CFLAGS = $(OPTS) -D_USRDLL -DISOLATION_AWARE_ENABLED=1 -I$(LIBDIR)
LDFLAGS = $(OPTS) -mwindows --target=i386-mingw32 --driver-name=gcc-3

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

OBJS = main.o settingdlg.o mailboxdlg.o $(LOBJS) extend.res
LOBJS = setting.o uri.o icon.o win32.o
LIBS = -lstdc++ -lshlwapi -lcomctl32 -lwinmm

RSRC = ja.rc

.SUFFIXES: .rc .res
.rc.res:	; windres -O coff -i $< -o $@

all: $(DLL)

$(DLL): $(OBJS)
	dllwrap $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(LOBJS): %.o: $(LIBDIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

$(OBJS): Makefile
main.o settingdlg.o mailboxdlg.o setting.o icon.o: $(LIBDIR)/win32.h
settingdlg.o mailboxdlg.o: settingdlg.h define.h $(LIBDIR)/setting.h
settingdlg.o: $(LIBDIR)/icon.h
mailboxdlg.o uri.o: $(LIBDIR)/mailbox.h
setting.o icon.o win32.o: %.o: $(LIBDIR)/%.h
extend.res: $(RSRC) define.h

clean: mostlyclean	; @$(RM) $(DLL)
distclean: clean	; @$(RM) *~
mostlyclean:		; @$(RM) $(OBJS)
