#
# Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
#
# This software comes with ABSOLUTELY NO WARRANTY; for details of
# the license terms, see the LICENSE.txt file included with the program.
#
NAME = befoo
OPTS = -mwin32 -m32 -mthreads
OPTS += -mno-cygwin
CFLAGS = $(OPTS)
LDFLAGS = $(OPTS) -mwindows

CFLAGS += -DUSE_OPENSSL=0 # use SSPI SChannel
CFLAGS += -DUSE_SSL2=0 # use TLSv1 (or SSLv3)

ifdef debug
CFLAGS += -D_DEBUG=$(debug) -g -Wall
LDFLAGS += -Wl,--subsystem,console
else
CFLAGS += -DNDEBUG -Os
LDFLAGS += -Wl,--strip-all -Wl,-O1 -Wl,--as-needed
endif
CPPFLAGS = $(CFLAGS)

PROG = $(NAME).exe
OBJS = main.o imap4.o pop3.o mail.o mailbox.o
OBJS += mascot.o summary.o setting.o window.o win32.o mingw/mthr.o
L10N = ja.rc
LIBS = -lstdc++ -lshlwapi -limagehlp -lws2_32 -lsecur32 -lcomctl32 -lwinmm

RSRC = $(PROG).manifest ../ico/app.ico
RSRC += ../ico/jump1.ico ../ico/jump2.ico ../ico/jump3.ico ../ico/jump4.ico
RSRC += ../ico/sleep1.ico ../ico/sleep2.ico ../ico/sleep3.ico ../ico/sleep4.ico

.SUFFIXES:
.SUFFIXES: .c .cpp .o .rc .res

.rc.res:
	windres --output-format=coff --input=$< --output=$@

all: $(PROG)

$(PROG): $(OBJS) $(NAME).res
	gcc $(LDFLAGS) -o $@ $(OBJS) $(NAME).res $(LIBS)

$(OBJS): win32.h Makefile
main.o mascot.o summary.o: define.h mailbox.h window.h setting.h
mail.o mailbox.o imap4.o pop3.o: mailbox.h
window.o: window.h
setting.o: setting.h
$(NAME).res: $(L10N) $(RSRC) define.h Makefile

clean:
	@rm -f *.o *.res *~ mingw/*.o
