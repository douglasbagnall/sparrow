all::

#CFLAGS =
#LDFLAGS =
ALL_CFLAGS =  $(VECTOR_FLAGS) -O3 -Wall -pipe -DDSFMT_MEXP=19937 $(INCLUDES) $(CFLAGS)
ALL_LDFLAGS = $(LDFLAGS)

DSFMT_FLAGS =  -finline-functions -fomit-frame-pointer -DNDEBUG -fno-strict-aliasing --param max-inline-insns-single=1800  -Wmissing-prototypes  -std=c99

VECTOR_FLAGS = -msse2 -DHAVE_SSE2 -D__SSE2__

SPARROW_SRC = gstsparrow.c dSFMT/dSFMT.c

CC = gcc
AR = ar
INSTALL = install

export GST_DEBUG = sparrow:4
#export GST_PLUGIN_PATH = .

#GST_PLUGIN_LDFLAGS = -module -avoid-version -export-symbols-regex '_*\(gst_\|Gst\|GST_\).*'
GST_INCLUDES =  -I/usr/include/gstreamer-0.10 -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include -I/usr/include/libxml2
INCLUDES = -I. $(GST_INCLUDES) -I/usr/include/libxml2 -I/usr/include/liboil-0.3

LINKS = -lgstbase-0.10 -lgstcontroller-0.10 -lgstreamer-0.10 -lgobject-2.0 \
	-lgmodule-2.0 -lgthread-2.0 -lrt -lxml2 -lglib-2.0 -lgstvideo-0.10

all:: libgstsparrow.so

libgstsparrow.so: gstsparrow.o dSFMT/dSFMT.o
	$(CC) -shared -Wl,-O1 $+ $(GST_PLUGIN_LDFLAGS)  $(INCLUDES) $(LINKS) -Wl,-soname -Wl,$@ \
	  -o $@

clean:
	rm -f *.so *.o *.a *.d
	cd dSFMT && rm -f *.o


dSFMT/dSFMT.o: dSFMT/dSFMT.c
	$(CC) $(INCLUDES) -MD $(ALL_CFLAGS) $(CPPFLAGS) $(VECTOR_FLAGS) $(DSFMT_FLAGS) -c -o $@ $<

.c.o:
	$(CC) $(INCLUDES) -c -MD $(ALL_CFLAGS) $(CPPFLAGS) -o $@ $<
#	$(CC) $(INCLUDES) -c $(ALL_CFLAGS) $(CPPFLAGS) -MD $<

sparrow_gamma_lut.h: gamma.py
	python $< > $@

gstsparrow.c: sparrow_gamma_lut.h gstsparrow.h

TEST_GST_ARGS =   --gst-plugin-path=. --gst-debug=myelement:5
TEST_V4L2_SHAPE = video/x-raw-yuv,width=640,height=480,framerate=25/1

test: all
	gst-launch $(TEST_GST_ARGS) v4l2src ! $(TEST_V4L2_SHAPE) ! ffmpegcolorspace  ! sparrow $(TEST_OPTIONS) ! ximagesink

test-pattern: all
	GST_DEBUG=sparrow:5 \
	gst-launch $(TEST_GST_ARGS) videotestsrc ! ffmpegcolorspace  ! sparrow $(TEST_OPTIONS) ! ximagesink

#TEST_VIDEO_FILE=/home/douglas/media/video/rochester-pal.avi
TEST_VIDEO_FILE=/home/douglas/tv/newartland_2008_ep2_ps6313_part3.flv

test-file: all
	gst-launch $(TEST_GST_ARGS) \
	filesrc location=$(TEST_VIDEO_FILE) ! decodebin2 \
	! ffmpegcolorspace ! sparrow $(TEST_OPTIONS) ! ximagesink

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
	cproto $(INCLUDES) -DUNUSED='' -s *.c


