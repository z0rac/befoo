#
# Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
#
# This software comes with ABSOLUTELY NO WARRANTY; for details of
# the license terms, see the LICENSE.txt file included with the program.
#
NAME = extend
OPTS = -mwin32 -m32
OPTS += -mno-cygwin
CFLAGS = $(OPTS) -DISOLATION_AWARE_ENABLED=1
LDFLAGS = $(OPTS) -mwindows --target=i386-mingw32

ifdef debug
CFLAGS += -D_DEBUG=$(debug) -g -Wall
else
CFLAGS += -DNDEBUG -Os
LDFLAGS += -Wl,--strip-all -Wl,-O1 -Wl,--as-needed
endif
CPPFLAGS = $(CFLAGS)

DLL = $(NAME).dll
OBJS = main.o settingdlg.o setting.o win32.o
L10N = ja.rc
LIBS = -lstdc++ -lshlwapi -lcomctl32 -lwinmm

RSRC = $(DLL).manifest

.SUFFIXES:
.SUFFIXES: .c .cpp .o .rc .res

.rc.res:
	windres --output-format=coff --input=$< --output=$@

all: $(DLL)

$(DLL): $(OBJS) $(NAME).res
	dllwrap $(LDFLAGS) -o $@ $(OBJS) $(NAME).res $(LIBS)

$(OBJS): win32.h Makefile
main.o: define.h
setting.o: setting.h
#main.o mascot.o summary.o: define.h mailbox.h window.h setting.h
#mail.o mailbox.o imap4.o pop3.o: mailbox.h
#window.o: window.h
$(NAME).res: $(L10N) $(RSRC) Makefile

clean:
	@rm -f *.o *.res *~
