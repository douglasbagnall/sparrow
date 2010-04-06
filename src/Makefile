all::

#CFLAGS =
#LDFLAGS =
ALL_CFLAGS = $(CFLAGS)   -O3 -Wall -pipe  -DHAVE_CONFIG_H $(INCLUDES)
ALL_LDFLAGS = $(LDFLAGS)


GST_LIBS = -lgstbase-0.10 -lgstcontroller-0.10 -lgstreamer-0.10 -lgobject-2.0 -lgmodule-2.0 -lgthread-2.0 -lrt -lxml2 -lglib-2.0
GST_PLUGIN_LDFLAGS = -module -avoid-version -export-symbols-regex _*\(gst_\|Gst\|GST_\).*



CC = gcc
AR = ar
INSTALL = install


#GST_SHARED = /usr/lib/libgstbase-0.10.so /usr/lib/libgstcontroller-0.10.so \
#	/usr/lib/libgstreamer-0.10.so /usr/lib/libgobject-2.0.so /usr/lib/libgmodule-2.0.so\
#	 /usr/lib/libgthread-2.0.so -lrt /usr/lib/libxml2.so /usr/lib/libglib-2.0.so

GST_INCLUDES =  -I/usr/include/gstreamer-0.10 -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include -I/usr/include/libxml2
INCLUDES = -I. -I..  $(GST_INCLUDES) -I/usr/include/libxml2 -I/usr/include/liboil-0.3

LINKS = -lgstbase-0.10 -lgstcontroller-0.10 -lgstreamer-0.10 -lgobject-2.0 \
	-lgmodule-2.0 -lgthread-2.0 -lrt -lxml2 -lglib-2.0 -lgstvideo-0.10


all:: libgstsparrow.so

libgstsparrow.so: gstsparrow.o
	$(CC) -shared -Wl,-O1 $< $(GST_SHARED)  $(INCLUDES) $(LINKS) -Wl,-soname -Wl,$@ \
	  -o $@

clean:
	rm -f *.so *.o *.a *.la

.c.o:
	$(CC) $(INCLUDES) -c -MD $(ALL_CFLAGS) $(CPPFLAGS) -o $@ $<
#	$(CC) $(INCLUDES) -c $(ALL_CFLAGS) $(CPPFLAGS) -MD $<

sparrow_gamma_lut.h: gamma.py
	python $< > $@

gstsparrow.c: sparrow_gamma_lut.h


TEST_GST_ARGS =   --gst-plugin-path=. --gst-debug=myelement:5

test: all
	gst-launch $(TEST_GST_ARGS) v4l2src ! ffmpegcolorspace  ! sparrow ! ximagesink

test-pattern: all
	gst-launch $(TEST_GST_ARGS) videotestsrc ! ffmpegcolorspace  ! sparrow ! ximagesink

#TEST_VIDEO_FILE=/home/douglas/media/video/rochester-pal.avi
TEST_VIDEO_FILE=/home/douglas/tv/newartland_2008_ep2_ps6313_part3.flv

test-file: all
	gst-launch $(TEST_GST_ARGS) \
	filesrc location=$(TEST_VIDEO_FILE) ! decodebin2 \
	! ffmpegcolorspace ! sparrow ! ximagesink

#show filtered and unfiltered video side by side
test-tee: all
	gst-launch  $(TEST_GST_ARGS) v4l2src ! tee name=vid2 \
	! queue ! ffmpegcolorspace  ! sparrow ! ximagesink \
	vid2. ! queue ! ffmpegcolorspace ! ximagesink

test-tee2: all
	gst-launch  $(TEST_GST_ARGS) -v v4l2src ! ffmpegcolorspace ! tee name=vid2 \
	! queue  ! sparrow ! ximagesink \
	vid2. ! queue ! fdsink  | \
	gst-launch fdsrc ! queue !  ximagesink



