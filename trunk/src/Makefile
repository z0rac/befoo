#
# Copyright (C) 2009-2012 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
#
# This software comes with ABSOLUTELY NO WARRANTY; for details of
# the license terms, see the LICENSE.txt file included with the program.
#
PROG = befoo.exe

ifeq ($(x64), 1)
BINPREFIX = x86_64-w64-mingw32-
else
BINPREFIX = i686-w64-mingw32-
endif
CC = $(BINPREFIX)gcc
CXX = $(BINPREFIX)g++

OPTS = -mthreads
CFLAGS = $(OPTS)
LDFLAGS = $(OPTS) -mwindows -static

ifdef USE_OPENSSL # use OpenSSL instead of SSPI SChannel.
CFLAGS += -DUSE_OPENSSL=$(USE_OPENSSL)
endif
ifdef USE_SSL2 # use SSLv2 instead of TLSv1 (or SSLv3).
CFLAGS += -DUSE_SSL2=$(USE_SSL2)
endif
ifdef USE_ICONV # use iconv instead of internal function.
CFLAGS += -DUSE_ICONV=$(USE_ICONV)
endif
ifdef USE_REG # use registory instead of .INI file.
CFLAGS += -DUSE_REG=$(USE_REG)
endif

ifdef debug
CFLAGS += -D_DEBUG=$(debug) -g -Wall
LDFLAGS += -Wl,--subsystem,console
else
CFLAGS += -DNDEBUG -Os
LDFLAGS += -Wl,--strip-all -Wl,-O1 -Wl,--as-needed
endif
CXXFLAGS = $(CFLAGS)

OBJS = main.o imap4.o pop3.o mail.o codepage.o mailbox.o uri.o
OBJS += mascot.o icon.o summary.o setting.o window.o win32.o winsock.o
OBJS += befoo.res
LIBS = -lstdc++ -lshlwapi -limagehlp -lws2_32 -lsecur32 -lcrypt32 -lcomctl32 -lwinmm

ICONSDIR = ../icons
RSRC = $(PROG).manifest $(ICONSDIR)/default.rc ja.rc

MAKECP = codepage.exe

.SUFFIXES: .rc .res
.rc.res:	; $(BINPREFIX)windres -I$(ICONSDIR) -O coff -i $< -o $@

all: $(PROG)

$(PROG): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

codepage.h: codepage.txt $(MAKECP)
	./$(MAKECP) < codepage.txt > $@

$(MAKECP): %.exe: %.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -DMAIN=1 -o $@ $<

$(OBJS): Makefile
main.o mascot.o summary.o: define.h mailbox.h setting.h window.h win32.h
mail.o uri.o imap4.o pop3.o: mailbox.h
main.o mailbox.o: win32.h winsock.h
icon.o mail.o setting.o window.o winsock.o: win32.h
mascot.o: icon.h
codepage.o icon.o mailbox.o setting.o window.o win32.o winsock.o: %.o: %.h
befoo.res: $(RSRC) define.h

.PHONY: clean distclean mostlyclean
clean: mostlyclean	; @$(RM) $(PROG)
distclean: clean	; @$(RM) $(MAKECP) *~
mostlyclean:		; @$(RM) $(OBJS)
