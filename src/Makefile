include ../config.mak

GLIB_CFLAGS = `pkg-config glib-2.0 --cflags gthread-2.0 gdk-2.0`
FFMPEG_CFLAGS = -I../libavformat -I../libavcodec
X_CFLAGS = -I/usr/X11R6/include
CFLAGS += -I. -I.. \
	$(OPTFLAGS) \
	$(GLIB_CFLAGS) \
	$(FFMPEG_CFLAGS) \
	$(X_CFLAGS)

LIBS  =  -L/usr/local/lib -lm -lOMC -lfl
GLIBS = `pkg-config glib-2.0 --libs gthread-2.0 gdk-2.0`
XLIBS = -L/usr/X11R6/lib -lX11 -lXext -lXv
FFLIBS = ../libavformat/libavformat.a ../libavcodec/libavcodec.a

LDFLAGS = $(LIBS) $(GLIBS) $(XLIBS) $(FFLIBS)

XINEMF_OBJ = xinemf.o \
	     displayer.o \
	     init.o \
	     playlist.o \
	     playlist-lexer.o \
	     playlist-parser.o \
	     netosc.o \
	     OSC-client.o \
	     htmsocket.o \
	     debug.o \
	     integrator.o \
	     eq.o

XINEMF_HEADERS = xinemf.h \
		 playlist.h

all: xinemf

xinemf.o: xinemf.h playlist.h
playlist.o: xinemf.h playlist.h
init.o: xinemf.h
netosc.o: xinemf.h

playlist-lexer.o: playlist-parser.o

xinemf: $(XINEMF_OBJ) $(FFLIBS)
	$(CC) $(XINEMF_OBJ) $(LDFLAGS) -o $@

clean:
	rm -rf $(XINEMF_OBJ) 
	rm -rf xinemf
