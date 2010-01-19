#
# Copyright (C) 2009-2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
#
# This software comes with ABSOLUTELY NO WARRANTY; for details of
# the license terms, see the LICENSE.txt file included with the program.
#
NAME = extend
OPTS = -mwin32 -m32
OPTS += -mno-cygwin
LIBDIR = ../src
CFLAGS = $(OPTS) -D_USRDLL -DISOLATION_AWARE_ENABLED=1 -I$(LIBDIR)
LDFLAGS = $(OPTS) -mwindows --target=i386-mingw32

ifdef debug
CFLAGS += -D_DEBUG=$(debug) -g -Wall
else
CFLAGS += -DNDEBUG -Os
LDFLAGS += -Wl,--strip-all -Wl,-O1 -Wl,--as-needed
endif
CXXFLAGS = $(CFLAGS)

DLL = $(NAME).dll
OBJS = main.o settingdlg.o
LOBJS = setting.o uri.o win32.o
OBJS += $(LOBJS)
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

$(LOBJS): %.o: $(LIBDIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

$(OBJS) $(NAME).res: Makefile
main.o settingdlg.o setting.o win32.o: $(LIBDIR)/win32.h
settingdlg.o: define.h $(LIBDIR)/setting.h $(LIBDIR)/mailbox.h
setting.o: $(LIBDIR)/setting.h
$(NAME).res: $(L10N) $(RSRC) define.h

clean: mostlyclean
	@$(RM) $(DLL)

distclean: clean
	@$(RM) *~

mostlyclean:
	@$(RM) $(OBJS) $(NAME).res
