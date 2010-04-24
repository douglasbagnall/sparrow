all::

#CFLAGS =
#LDFLAGS =
ALL_CFLAGS =  $(VECTOR_FLAGS) -O3 -Wall -pipe -DDSFMT_MEXP=19937 -std=gnu99 $(INCLUDES) $(CFLAGS)
ALL_LDFLAGS = $(LDFLAGS)

DSFMT_FLAGS =  -finline-functions -fomit-frame-pointer -DNDEBUG -fno-strict-aliasing --param max-inline-insns-single=1800  -Wmissing-prototypes  -std=c99

VECTOR_FLAGS = -msse2 -DHAVE_SSE2 -D__SSE2__

# these *might* do something useful
#POSSIBLE_OPTIMISING_CFLAGS = -fmodulo-sched -fmodulo-sched-allow-regmoves -fgcse-sm -fgcse-las \
# -funsafe-loop-optimizations -Wunsafe-loop-optimizations -fsee and more
#POSSIBLE_PESSIMISING_CFLAGS -fmudflap -fmudflapth -fmudflapir

SPARROW_SRC = gstsparrow.c dSFMT/dSFMT.c sparrow.c

CC = gcc
AR = ar
INSTALL = install

export GST_DEBUG = sparrow:4
#export GST_PLUGIN_PATH = .

#GST_PLUGIN_LDFLAGS = -module -avoid-version -export-symbols-regex '_*\(gst_\|Gst\|GST_\).*'
GST_INCLUDES =  -I/usr/include/gstreamer-0.10 -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include -I/usr/include/libxml2
INCLUDES = -I. $(GST_INCLUDES) -I/usr/include/liboil-0.3 -I/usr/include/opencv/

LINKS = -lgstbase-0.10 -lgstcontroller-0.10 -lgstreamer-0.10 -lgobject-2.0 \
	-lgmodule-2.0 -lgthread-2.0 -lrt -lxml2 -lglib-2.0 -lgstvideo-0.10 \
	-lcxcore -lcv -lcvaux -lhighgui

all:: libgstsparrow.so

libgstsparrow.so: gstsparrow.o sparrow.o dSFMT/dSFMT.o
	$(CC) -shared -Wl,-O1 $+ $(GST_PLUGIN_LDFLAGS)  $(INCLUDES) $(LINKS) -Wl,-soname -Wl,$@ \
	  -o $@

clean:
	rm -f *.so *.o *.a *.d *.s
	cd dSFMT && rm -f *.o *.s
	rm -f sparrow_false_colour_lut.h sparrow_gamma_lut.h

dSFMT/dSFMT.o: dSFMT/dSFMT.c
	$(CC) $(INCLUDES) -MD $(ALL_CFLAGS) $(CPPFLAGS) $(DSFMT_FLAGS) -c -o $@ $<

.c.o:
	$(CC) $(INCLUDES) -c -MD $(ALL_CFLAGS) $(CPPFLAGS) -o $@ $<
#	$(CC) $(INCLUDES) -c $(ALL_CFLAGS) $(CPPFLAGS) -MD $<

%.s:	%.c
	$(CC) $(INCLUDES) -S  $(ALL_CFLAGS) $(CPPFLAGS) -o $@ $<

sparrow_gamma_lut.h: gamma.py
	python $< > $@

sparrow_false_colour_lut.h: false_colour.py
	python $< > $@

gstsparrow.c: sparrow_gamma_lut.h gstsparrow.h sparrow_false_colour_lut.h sparrow.h

sparrow.c: sparrow_gamma_lut.h gstsparrow.h sparrow_false_colour_lut.h sparrow.h

TEST_GST_ARGS =   --gst-plugin-path=. --gst-debug=sparrow:5
TEST_V4L2_SHAPE = 'video/x-raw-yuv,format=(fourcc)YUY2,width=320,height=240,framerate=25/1'

test: all
	gst-launch $(TEST_GST_ARGS) v4l2src ! $(TEST_V4L2_SHAPE) ! ffmpegcolorspace  ! sparrow $(TEST_OPTIONS) ! ximagesink

test-cam:
	gst-launch $(TEST_GST_ARGS) v4l2src ! $(TEST_V4L2_SHAPE) ! ffmpegcolorspace  ! ximagesink

test-pattern: all
	GST_DEBUG=sparrow:5 \
	gst-launch $(TEST_GST_ARGS) videotestsrc ! ffmpegcolorspace  ! sparrow $(TEST_OPTIONS) ! ximagesink

TEST_VIDEO_FILE=/home/douglas/media/video/rochester-pal.avi
#TEST_VIDEO_FILE=/home/douglas/tv/newartland_2008_ep2_ps6313_part3.flv

test-file: all
	gst-launch $(TEST_GST_ARGS) \
	filesrc location=$(TEST_VIDEO_FILE) ! decodebin2 \
	! ffmpegcolorspace ! sparrow $(TEST_OPTIONS) ! ximagesink

inspect: all
	gst-inspect $(TEST_GST_ARGS)  sparrow $(TEST_OPTIONS)


#show filtered and unfiltered video side by side
test-tee: all
	gst-launch  $(TEST_GST_ARGS) v4l2src ! tee name=vid2 \
	! queue ! ffmpegcolorspace  ! sparrow $(TEST_OPTIONS) ! ximagesink \
	vid2. ! queue ! ffmpegcolorspace ! ximagesink

test-tee2: all
	gst-launch  $(TEST_GST_ARGS) -v v4l2src ! ffmpegcolorspace ! tee name=vid2 \
	! queue  ! sparrow $(TEST_OPTIONS) ! ximagesink \
	vid2. ! queue ! fdsink  | \
	gst-launch fdsrc ! queue !  ximagesink


TAGS:
	$(RM) TAGS
#	find  -name "*.[ch]" | xargs etags -a
	etags -R

.PHONY: TAGS all cproto

cproto:
#	cproto $(INCLUDES) -DUNUSED='' -S -i -X 0 *.c
	cproto $(INCLUDES) -DUNUSED='' -S -X 0 *.c


