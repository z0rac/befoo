#
# Copyright (C) 2009-2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
#
# This software comes with ABSOLUTELY NO WARRANTY; for details of
# the license terms, see the LICENSE.txt file included with the program.
#
NAME = befoo
OPTS = -mwin32 -m32 -mthreads
OPTS += -mno-cygwin
CFLAGS = $(OPTS)
LDFLAGS = $(OPTS) -mwindows

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

PROG = $(NAME).exe
OBJS = main.o imap4.o pop3.o mail.o codepage.o mailbox.o uri.o
OBJS += mascot.o summary.o setting.o window.o win32.o winsock.o mingw/mthr.o
L10N = ja.rc
LIBS = -lstdc++ -lshlwapi -limagehlp -lws2_32 -lsecur32 -lcrypt32 -lcomctl32 -lwinmm

RSRC = $(PROG).manifest ../ico/app.ico
RSRC += ../ico/jump1.ico ../ico/jump2.ico ../ico/jump3.ico ../ico/jump4.ico
RSRC += ../ico/sleep1.ico ../ico/sleep2.ico ../ico/sleep3.ico ../ico/sleep4.ico

MAKECP = codepage.exe

.SUFFIXES:
.SUFFIXES: .c .cpp .o .rc .res

.rc.res:
	windres --output-format=coff --input=$< --output=$@

all: $(PROG)

$(PROG): $(OBJS) $(NAME).res
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(NAME).res $(LIBS)

codepage.h: codepage.txt $(MAKECP)
	./$(MAKECP) < codepage.txt > $@

$(MAKECP): %.exe: %.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -DMAIN=1 -o $@ $<

$(OBJS) $(NAME).res: Makefile
main.o mascot.o summary.o: define.h mailbox.h setting.h window.h win32.h
mail.o uri.o imap4.o pop3.o: mailbox.h
main.o mailbox.o: win32.h winsock.h
mail.o setting.o window.o winsock.o: win32.h
codepage.o mailbox.o setting.o window.o win32.o winsock.o: %.o: %.h
$(NAME).res: $(L10N) $(RSRC) define.h

clean: mostlyclean
	@$(RM) $(PROG)

distclean: clean
	@$(RM) $(MAKECP) *~

mostlyclean:
	@$(RM) $(OBJS) $(NAME).res
