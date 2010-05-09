all::

#CFLAGS =
#LDFLAGS =
DEFINES = -DDSFMT_MEXP=19937
WARNINGS = -Wall -Wextra -Wno-unused-parameter
ALL_CFLAGS =  $(VECTOR_FLAGS) -O3 $(WARNINGS) -pipe -DDSFMT_MEXP=19937 -std=gnu99 $(INCLUDES) $(CFLAGS)
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

#OPENCV_INCLUDE = -I/usr/include/opencv/
OPENCV_INCLUDE = -isystem /usr/local/include/opencv/

#GST_PLUGIN_LDFLAGS = -module -avoid-version -export-symbols-regex '_*\(gst_\|Gst\|GST_\).*'
GST_INCLUDES =  -I/usr/include/gstreamer-0.10 -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include -I/usr/include/libxml2
INCLUDES = -I. $(GST_INCLUDES) -I/usr/include/liboil-0.3 $(OPENCV_INCLUDE)

LINKS = -L/usr/local/lib -lgstbase-0.10 -lgstreamer-0.10 -lgobject-2.0 \
	-lglib-2.0 -lgstvideo-0.10 -lcxcore
#  -lgstcontroller-0.10 -lgmodule-2.0 -lgthread-2.0 -lrt -lxml2  -lcv -lcvaux -lhighgui

all:: libgstsparrow.so

libgstsparrow.so: gstsparrow.o sparrow.o dSFMT/dSFMT.o
	$(CC) -shared -Wl,-O1 $+ $(GST_PLUGIN_LDFLAGS)  $(INCLUDES) $(DEFINES)  $(LINKS) -Wl,-soname -Wl,$@ \
	  -o $@

clean:
	rm -f *.so *.o *.a *.d *.s
	cd dSFMT && rm -f *.o *.s
	rm -f sparrow_false_colour_lut.h sparrow_gamma_lut.h

dSFMT/dSFMT.o: dSFMT/dSFMT.c
	$(CC)  $(DSFMT_FLAGS) $(INCLUDES) -MD $(ALL_CFLAGS)  -fvisibility=hidden  $(CPPFLAGS) -c -o $@ $<

.c.o:
	$(CC) $(INCLUDES) -c -MD $(ALL_CFLAGS) $(CPPFLAGS) -o $@ $<
#	$(CC) $(INCLUDES) -c $(ALL_CFLAGS) $(CPPFLAGS) -MD $<

%.s:	%.c
	$(CC) $(INCLUDES) -S  $(ALL_CFLAGS) $(CPPFLAGS) -o $@ $<

%.i:	%.c
	$(CC) $(INCLUDES) -E  $(ALL_CFLAGS) $(CPPFLAGS) -o $@ $<

sparrow_gamma_lut.h: gamma.py
	python $< > $@

sparrow_false_colour_lut.h: false_colour.py
	python $< > $@

gstsparrow.c: sparrow_gamma_lut.h gstsparrow.h sparrow_false_colour_lut.h sparrow.h

sparrow.c: sparrow_gamma_lut.h gstsparrow.h sparrow_false_colour_lut.h sparrow.h

DEBUG_LEVEL = 5
TEST_GST_ARGS =   --gst-plugin-path=. --gst-debug=sparrow:$(DEBUG_LEVEL)
TEST_INPUT_SIZE = width=320,height=240
TEST_OUTPUT_SIZE = width=320,height=240
TEST_V4L2_SHAPE = 'video/x-raw-yuv,format=(fourcc)YUY2,$(TEST_INPUT_SIZE),framerate=25/1'
TEST_OUTPUT_SHAPE = 'video/x-raw-rgb,$(TEST_OUTPUT_SIZE),framerate=25/1'
TEST_SINK = ximagesink
#TEST_SINK = fbdevsink
TEST_PIPE_TAIL =   ffmpegcolorspace  ! sparrow $(TEST_OPTIONS) ! $(TEST_OUTPUT_SHAPE) ! $(TEST_SINK)
TEST_V4L2_PIPE_TAIL = $(TEST_V4L2_SHAPE) ! $(TEST_PIPE_TAIL)

test: all
	gst-launch $(TEST_GST_ARGS) v4l2src ! $(TEST_V4L2_PIPE_TAIL)

test-times: all
	timeout -3 20 time -v gst-launch $(TEST_GST_ARGS) v4l2src ! $(TEST_V4L2_PIPE_TAIL)

test-cam:
	gst-launch $(TEST_GST_ARGS) v4l2src ! $(TEST_V4L2_PIPE_TAIL)

test-pattern: all
	GST_DEBUG=sparrow:5 \
	gst-launch $(TEST_GST_ARGS) videotestsrc ! $(TEST_V4L2_PIPE_TAIL)

TEST_VIDEO_FILE=/home/douglas/media/video/rochester-pal.avi
#TEST_VIDEO_FILE=/home/douglas/tv/newartland_2008_ep2_ps6313_part3.flv

test-file: all
	gst-launch $(TEST_GST_ARGS) \
	filesrc location=$(TEST_VIDEO_FILE) ! decodebin2 ! $(TEST_PIPE_TAIL)

inspect: all
	gst-inspect $(TEST_GST_ARGS)  sparrow $(TEST_OPTIONS)


#show filtered and unfiltered video side by side
test-tee: all
	gst-launch  $(TEST_GST_ARGS) v4l2src ! tee name=vid2 \
	! queue ! ffmpegcolorspace  ! sparrow $(TEST_OPTIONS) ! $(TEST_OUTPUT_SHAPE) ! $(TEST_SINK) \
	vid2. ! queue ! ffmpegcolorspace ! $(TEST_OUTPUT_SHAPE) ! $(TEST_SINK)

test-tee2: all
	gst-launch  $(TEST_GST_ARGS) -v v4l2src ! ffmpegcolorspace ! tee name=vid2 \
	! queue  ! sparrow $(TEST_OPTIONS) ! $(TEST_OUTPUT_SHAPE) ! $(TEST_SINK) \
	vid2. ! queue ! fdsink  | \
	gst-launch fdsrc ! queue !  $(TEST_OUTPUT_SHAPE) ! $(TEST_SINK)


TAGS:
	$(RM) TAGS
#	find  -name "*.[ch]" | xargs etags -a
	etags -R --exclude=junk --exclude=.git --exclude=prof

cscope:
	$(RM) cscope.out
	cscope -b $(shell echo "$(INCLUDES)" | sed s/-isystem/-I/)

cproto:
#	cproto $(INCLUDES) -DUNUSED='' -S -i -X 0 *.c
	cproto $(shell echo "$(INCLUDES)" | sed s/-isystem/-I/) -DUNUSED=''  $(DEFINES) -S -X 0 *.c

cproto-nonstatic:
	cproto $(INCLUDES) -DUNUSED=''  $(DEFINES)  -X 0 *.c


#oprofile: all
#	sudo opcontrol --no-vmlinux $(OP_OPTS) && sudo opcontrol $(OP_OPTS) --start --verbose
#	timeout -3 10 gst-launch $(TEST_GST_ARGS) v4l2src ! $(TEST_V4L2_SHAPE) ! ffmpegcolorspace \
#	! sparrow $(TEST_OPTIONS) ! $(TEST_OUTPUT_SHAPE) ! $(TEST_SINK)
#	opreport $(OP_OPTS)

sysprof:
	make clean
	make CFLAGS='-g -fno-inline -fno-inline-functions -fno-omit-frame-pointer'
	lsmod | grep -q 'sysprof_module' || sudo modprobe sysprof-module
	sysprof &
	@echo "click the start button!"

splint:
	splint $(INCLUDES) sparrow.c
	flawfinder $(PWD)


unittest:
	$(CC) $(INCLUDES) -MD $(ALL_CFLAGS) $(CPPFLAGS) -o test test.c
	./test

unittest-shifts:
	$(CC)  -MD $(ALL_CFLAGS) $(CPPFLAGS) -o test shift_test.c
	./test

CV_LINKS = -lcv -lcvaux -lhighgui

unittest-edges:
	$(CC)  -MD $(ALL_CFLAGS) $(CPPFLAGS) $(CV_LINKS) -o test test-find-edge.c
	./test


.PHONY: TAGS all cproto cproto-nonstatic sysprof splint
