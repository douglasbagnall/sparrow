all::

#CFLAGS =
#LDFLAGS =
DEFINES = -DDSFMT_MEXP=19937
WARNINGS = -Wall -Wextra -Wno-unused-parameter

ARCH = $(shell arch)
ifeq "$(ARCH)" "x86_64"
ARCH_CFLAGS = -fPIC -DPIC -m64
JPEG_LIBRARY_PATH=/opt/libjpeg-turbo/lib64
else
ARCH_CFLAGS = -m32 -msse2
JPEG_LIBRARY_PATH=/opt/libjpeg-turbo/lib32
endif

JPEG_CPATH = /opt/libjpeg-turbo/include
CPATH = $(JPEG_CPATH)
LD_LIBRARY_PATH = $(JPEG_LIBRARY_PATH):$(LD_LIBRARY_PATH)


ALL_CFLAGS = -march=native -pthread $(VECTOR_FLAGS) -O3 $(WARNINGS) -pipe  -D_GNU_SOURCE -DDSFMT_MEXP=19937 -std=gnu99 $(INCLUDES) $(ARCH_CFLAGS) $(CFLAGS)
ALL_LDFLAGS = $(LDFLAGS)


DSFMT_FLAGS =  -finline-functions -fomit-frame-pointer -DNDEBUG -fno-strict-aliasing --param max-inline-insns-single=1800  -Wmissing-prototypes  -std=c99

VECTOR_FLAGS = -msse2 -DHAVE_SSE2 -D__SSE2__ -floop-strip-mine -floop-block

# these *might* do something useful
# -fvisibility=hidden
#POSSIBLE_OPTIMISING_CFLAGS = -fmodulo-sched -fmodulo-sched-allow-regmoves -fgcse-sm -fgcse-las \
# -funsafe-loop-optimizations -Wunsafe-loop-optimizations -fsee -funsafe-math-optimizations and more
# "-combine -fwhole-program" with __attribute__((externally_visible))
# -fprofile-arcs and -fbranch-probabilities
#POSSIBLE_PESSIMISING_CFLAGS -fmudflap -fmudflapth -fmudflapir

SPARROW_SRC = gstsparrow.c dSFMT/dSFMT.c sparrow.c

CC = gcc
AR = ar
INSTALL = install

export GST_DEBUG = sparrow:4
#export GST_PLUGIN_PATH = .

OPENCV_PREFIX = $(shell test -d /usr/local/include/opencv && echo /usr/local || echo /usr )

#OPENCV_INCLUDE = -I/usr/include/opencv/
OPENCV_INCLUDE = -isystem $(OPENCV_PREFIX)/include/opencv/

GTK_INCLUDES = -I/usr/include/gtk-2.0/ -I/usr/include/cairo/ -I/usr/include/pango-1.0/ -I/usr/lib/gtk-2.0/include/ -I/usr/include/atk-1.0/

#GST_PLUGIN_LDFLAGS = -module -avoid-version -export-symbols-regex '_*\(gst_\|Gst\|GST_\).*'
GST_INCLUDES =  -I/usr/include/gstreamer-0.10 -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include -I/usr/include/libxml2
INCLUDES = -I. $(GST_INCLUDES) -I/usr/include/liboil-0.3 $(OPENCV_INCLUDE) -I$(JPEG_CPATH)

JPEG_LINKS = -L$(JPEG_LIBRARY_PATH) -Wl,-Bstatic -ljpeg -Wl,-Bdynamic
# -L$(JPEG_LIBRARY_PATH) -Wl,-Bstatic -ljpeg -Wl,-Bdynamic
#or, to use dynamic, allegedly, -R $(JPEG_LIBRARY_PATH)
# or just put in the list of linkees $(JPEG_LIBRARY_PATH)/libjpeg.a
JPEG_STATIC=$(JPEG_LIBRARY_PATH)/libjpeg.a

LINKS = -L/usr/local/lib -lgstbase-0.10 -lgstreamer-0.10 -lgobject-2.0 \
	-lglib-2.0 -lgstvideo-0.10 -lcxcore -lcv $(JPEG_LINKS)
#  -lgstcontroller-0.10 -lgmodule-2.0 -lgthread-2.0 -lrt -lxml2  -lcv -lcvaux -lhighgui

SOURCES = gstsparrow.c sparrow.c calibrate.c play.c floodfill.c edges.c dSFMT/dSFMT.c jpeg_src.c load_images.c
OBJECTS := $(patsubst %.c,%.o,$(SOURCES))

all:: libgstsparrow.so

libgstsparrow.so: $(OBJECTS)
	$(CC) -shared -Wl,-O1 $+ $(GST_PLUGIN_LDFLAGS)  $(INCLUDES) $(DEFINES)  $(LINKS) -Wl,-soname -Wl,$@ \
	  -o $@

clean:
	rm -f *.so *.o *.a *.d *.s
	cd dSFMT && rm -f *.o *.s
	rm -f sparrow_false_colour_lut.h

dSFMT/dSFMT.o: dSFMT/dSFMT.c
	$(CC)  $(DSFMT_FLAGS)  -MD $(ALL_CFLAGS)  -fvisibility=hidden  $(CPPFLAGS) -c -o $@ $<

.c.o:
#	@echo $(CPATH)
#	@echo $(LIBRARY_PATH)
	$(CC)  -c -MD $(ALL_CFLAGS) $(CPPFLAGS) -o $@ $<
#	$(CC)  -c $(ALL_CFLAGS) $(CPPFLAGS) -MD $<

%.s:	%.c
	$(CC)  -S  $(ALL_CFLAGS) $(CPPFLAGS) -o $@ $<

%.i:	%.c
	$(CC)  -E  $(ALL_CFLAGS) $(CPPFLAGS) -o $@ $<

sparrow_false_colour_lut.h: false_colour.py
	python $< > $@

gstsparrow.c: gstsparrow.h sparrow_false_colour_lut.h sparrow.h

sparrow.c: gstsparrow.h sparrow_false_colour_lut.h sparrow.h

GST_LAUNCH = gst-launch-0.10
DEBUG_LEVEL = 5
TEST_GST_ARGS =   --gst-plugin-path=. --gst-debug=sparrow:$(DEBUG_LEVEL)
#TEST_INPUT_SIZE = width=320,height=240
#TEST_OUTPUT_SIZE = width=320,height=240
TEST_FPS=20
TEST_INPUT_SIZE = width=800,height=600
TEST_OUTPUT_SIZE = width=800,height=600

TEST_V4L2_SHAPE = 'video/x-raw-yuv,format=(fourcc)YUY2,$(TEST_INPUT_SIZE),framerate=$(TEST_FPS)/1'
TEST_OUTPUT_SHAPE = 'video/x-raw-rgb,$(TEST_OUTPUT_SIZE),framerate=$(TEST_FPS)/1'
TEST_SINK = ximagesink
#TEST_SINK = fbdevsink
TEST_PIPE_TAIL =   ffmpegcolorspace  ! sparrow $(TEST_OPTIONS) ! $(TEST_OUTPUT_SHAPE) ! $(TEST_SINK)
TEST_V4L2_PIPE_TAIL = $(TEST_V4L2_SHAPE) ! $(TEST_PIPE_TAIL)

test: all
	$(GST_LAUNCH) $(TEST_GST_ARGS) v4l2src ! $(TEST_V4L2_PIPE_TAIL)

test-capture: all
	$(GST_LAUNCH)  $(TEST_GST_ARGS) v4l2src ! ffmpegcolorspace ! tee name=vid2 \
	! queue  ! sparrow $(TEST_OPTIONS) ! $(TEST_OUTPUT_SHAPE) ! $(TEST_SINK) \
	vid2. ! queue ! ffmpegcolorspace ! theoraenc ! oggmux ! filesink location='/tmp/sparrow.ogv'

test-gtk: all
	GST_DEBUG=sparrow:$(DEBUG_LEVEL) ./gtk-app 2> /tmp/gst.log || less -R /tmp/gst.log

test-valgrind: debug
	valgrind --log-file=valgrind.log --trace-children=yes --suppressions=valgrind-python.supp \
	 $(GST_LAUNCH) $(TEST_GST_ARGS) v4l2src ! $(TEST_V4L2_PIPE_TAIL) 2> gst.log

test-gdb: debug
	echo "set args $(TEST_GST_ARGS) v4l2src ! $(TEST_V4L2_PIPE_TAIL)" > /tmp/gdb-args.txt
	gdb -x /tmp/gdb-args.txt $(GST_LAUNCH)

test-times: all
	timeout -3 20 time -v $(GST_LAUNCH) $(TEST_GST_ARGS) v4l2src ! $(TEST_V4L2_PIPE_TAIL)

test-cam:
	$(GST_LAUNCH) $(TEST_GST_ARGS) v4l2src ! $(TEST_V4L2_PIPE_TAIL)

test-pattern: all
	GST_DEBUG=sparrow:5 \
	$(GST_LAUNCH) $(TEST_GST_ARGS) videotestsrc ! $(TEST_V4L2_PIPE_TAIL)

TEST_VIDEO_FILE=/home/douglas/media/video/rochester-pal.avi
#TEST_VIDEO_FILE=/home/douglas/tv/newartland_2008_ep2_ps6313_part3.flv

test-file: all
	$(GST_LAUNCH) $(TEST_GST_ARGS) \
	filesrc location=$(TEST_VIDEO_FILE) ! decodebin2 ! $(TEST_PIPE_TAIL)

inspect: all
	gst-inspect $(TEST_GST_ARGS)  sparrow $(TEST_OPTIONS)


#show filtered and unfiltered video side by side
test-tee: all
	$(GST_LAUNCH)  $(TEST_GST_ARGS) v4l2src ! ffmpegcolorspace ! tee name=vid2 \
	! queue  ! sparrow $(TEST_OPTIONS) ! $(TEST_OUTPUT_SHAPE) ! $(TEST_SINK) \
	vid2. ! queue  ! sparrow $(TEST_OPTIONS) ! $(TEST_OUTPUT_SHAPE) ! $(TEST_SINK)


TAGS:
	$(RM) TAGS
#	find  -name "*.[ch]" | xargs etags -a
	etags -R --exclude=junk --exclude=.git --exclude=prof

cscope:
	$(RM) cscope.out
	cscope -b $(shell echo "$(INCLUDES)" | sed s/-isystem/-I/)


CPROTO_INCLUDES = $(shell echo "$(INCLUDES)" | sed s/-isystem/-I/)

cproto:
#	cproto $(INCLUDES) -DUNUSED='' -S -i -X 0 *.c
	cproto $(CPROTO_INCLUDES) -DUNUSED=''  $(DEFINES) -S -X 0 *.c

cproto-nonstatic:
	cproto $(CPROTO_INCLUDES) -DUNUSED=''  $(DEFINES)  -X 0 *.c


#oprofile: all
#	sudo opcontrol --no-vmlinux $(OP_OPTS) && sudo opcontrol $(OP_OPTS) --start --verbose
#	timeout -3 10 $(GST_LAUNCH) $(TEST_GST_ARGS) v4l2src ! $(TEST_V4L2_SHAPE) ! ffmpegcolorspace \
#	! sparrow $(TEST_OPTIONS) ! $(TEST_OUTPUT_SHAPE) ! $(TEST_SINK)
#	opreport $(OP_OPTS)

sysprof: debug
	lsmod | grep -q 'sysprof_module' || sudo modprobe sysprof-module || \
	echo "try again after 'sudo m-a a-i sysprof-module-source'"
	sysprof &
	@echo "click the start button!"

splint:
	splint $(INCLUDES) sparrow.c
	flawfinder $(PWD)


unittest:
	$(CC)  -MD $(ALL_CFLAGS) $(CPPFLAGS) -o test test.c
	./test

unittest-shifts:
	$(CC)  -MD $(ALL_CFLAGS) $(CPPFLAGS) -o test shift_test.c
	./test

CV_LINKS = -lcv -lcvaux -lhighgui

unittest-edges:
	$(CC)  -MD $(ALL_CFLAGS) $(CPPFLAGS) $(CV_LINKS) -o test test-find-edge.c
	./test

unittest-median:
	$(CC)  -MD $(ALL_CFLAGS) $(CPPFLAGS) $(CV_LINKS) -o test test-median.c
	./test

unittest-jpeg: gstsparrow.o sparrow.o calibrate.o play.o floodfill.o edges.o dSFMT/dSFMT.o jpeg_src.o
	$(CC)  -MD $(ALL_CFLAGS) $(CPPFLAGS) $(LINKS)  -o test $^ $(JPEG_STATIC)  test-jpeg.c

#	./test

debug:
	make -B CFLAGS='-g -fno-inline -fno-inline-functions -fno-omit-frame-pointer'

#ccmalloc
ccmalloc:
	make -B CFLAGS='-lccmalloc -g' CC='ccmalloc --nowrapper gcc'

rsync:
	rsync -t $(shell git ls-tree -r --name-only HEAD) 10.42.43.10:sparrow


.PHONY: TAGS all cproto cproto-nonstatic sysprof splint unittest unittest-shifts unittest-edges \
	debug ccmalloc rsync app-clean

GTK_APP = gtk-app.c
GTK_LINKS = -lglib-2.0 $(LINKS) -lgstinterfaces-0.10
CLUTTER_INCLUDES = -I/usr/include/clutter-1.0/ -I/usr/include/glib-2.0/ -I/usr/lib/glib-2.0/include/ -I/usr/include/pango-1.0/ -I/usr/include/cairo/ -I/usr/include/gstreamer-0.10/ -I/usr/include/libxml2/
CLUTTER_SRC = clutter-app.c
CLUTTER_LINKS = -lclutter-gst-0.10  -lglib-2.0
GTK_CLUTTER_LINKS =  $(LINKS) -lgstinterfaces-0.10 -lclutter-gst-0.10  -lglib-2.0 -lclutter-gtk-0.10
GTK_CLUTTER_INCLUDES =  $(GTK_INCLUDES) $(CLUTTER_INCLUDES)

gtk-app::
	$(CC)  $(ALL_CFLAGS) $(CPPFLAGS) $(CV_LINKS) $(INCLUDES) $(GTK_INCLUDES)\
	  $(GTK_LINKS) -o $@ $(GTK_APP)

gtk-clutter-app::
	$(CC)  $(ALL_CFLAGS) $(CPPFLAGS) $(GTK_CLUTTER_LINKS) $(INCLUDES) $(GTK_CLUTTER_INCLUDES) \
		 -o $@ gtk-clutter-app.c

sparrow.xml::
	$(GST_LAUNCH) -o sparrow.xml $(TEST_GST_ARGS) v4l2src ! $(TEST_V4L2_PIPE_TAIL)

clutter-app:
	$(CC)  $(ALL_CFLAGS) $(CPPFLAGS) $(CLUTTER_LINKS) $(CLUTTER_INCLUDES)\
	  $(LINKS)  -o $@ $(CLUTTER_SRC)

app-clean:
	$(RM) gtk-app clutter-app

