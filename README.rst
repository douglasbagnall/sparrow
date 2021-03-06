Libgstsparrow
=============

Libgstsparrow is a Gstreamer plug-in that obliterates everything it sees,
replacing it with images of sparrows.

It is designed to be used with a camera and projector pointed at the
same wall.  After a calibration phase, it will project a negative
image of whatever is visible but not wanted on the wall.  The
combination of the original and negative images is a flat grey, and
images of sparrows are superimposed on this by adding or subtracting
from the projected negative. It works reasonably well for most images
on flat surfaces.

Exhibition
==========

The plug-in was made for an artwork first exhibited at the Dowse Art
Gallery in Lower Hutt, NZ, in June 2010.  It consists of two of these
plugins, each with its own projector, competing for control of a
single screen.  Each projection works to cancel out the other, and the
resultant complex feedback loops make for occasionally interesting
video.  People can disrupt the flow of the work by moving in front of
the projectors and camera, the aftereffects of which can last for
several seconds.

Dependencies
============

Gstreamer, including dev packages.

OpenCV. On 32 bit x86 it is worth compiling it yourself for the SSE2
speed up.  That comes for free with AMD64.

libjpeg-turbo. http://libjpeg-turbo.virtualgl.org.  In modern distros
it is already the default. In the old days you had to compile it
yourself and possibly symlink ``/usr/lib/libjpeg*`` to their
counterparts in ``/opt/libjpeg-turbo`` (I mean, I did, but I can't
recall whether it was necessary in the end).


Compiling
=========

Try ``make && make test``.  There isn't an ``install`` target.
(``make test`` uses ``gst-launch-0.10 --gst-plugin-path=.``).


Importing images
================

Before you can show sparrows you need some pictures to show.

There are a couple of python scripts in that will convert video
streams into the correct format.

TODO: provide images, instructions.

Copyright and License
=====================

Copyright 2010 Douglas Bagnall

Portions copyright Gstreamer developers.

Licensed under the Gnu Lesser General Public License Version 2.1 or
greater.  See COPYING for license details.
