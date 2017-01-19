# Unix Makefile for RemGlk library

# This generates two files. One, of course, is libremglk.a -- the library
# itself. The other is Make.remglk; this is a snippet of Makefile code
# which locates the remglk library and associated libraries.
#
# When you install remglk, you must put libremglk.a in the lib directory,
# and glk.h, glkstart.h, and Make.remglk in the include directory.

# Pick a C compiler.
#CC = cc
CC = gcc -ansi

OPTIONS = -g -Wall

CFLAGS = $(OPTIONS) $(INCLUDEDIRS)

GLKLIB = libremglk.a

REMGLK_OBJS = \
  main.o rgevent.o rgfref.o rggestal.o \
  rgdata.o rgmisc.o rgstream.o rgstyle.o \
  rgwin_blank.o rgwin_buf.o rgwin_grid.o rgwin_pair.o rgwin_graph.o \
  rgwindow.o rgschan.o rgblorb.o \
  cgunicod.o cgdate.o gi_dispa.o gi_debug.o gi_blorb.o

REMGLK_HEADERS = \
  remglk.h rgdata.h rgwin_blank.h rgwin_buf.h \
  rgwin_grid.h rgwin_graph.h rgwin_pair.h gi_debug.h gi_dispa.h

all: $(GLKLIB) Make.remglk

cgunicod.o: cgunigen.c

$(GLKLIB): $(REMGLK_OBJS)
	ar r $(GLKLIB) $(REMGLK_OBJS)
	ranlib $(GLKLIB)

Make.remglk:
	echo LINKLIBS = $(LIBDIRS) $(LIBS) > Make.remglk
	echo GLKLIB = -lremglk >> Make.remglk

$(REMGLK_OBJS): glk.h $(REMGLK_HEADERS)

clean:
	rm -f *~ *.o $(GLKLIB) Make.remglk
