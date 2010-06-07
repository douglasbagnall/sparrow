#!/usr/bin/python

import os, sys
import struct
import os, sys, re

from sparrow import FRAME_STRUCTURE, INDEX_FILE, TEXT_INDEX_FILE
from sparrow import save_frames, save_frames_text, Frame, link_frames

#            f.write(struct.pack('II48s8I',
#                                frame.glob_index,
#                                frame.jpeg_len,
#                                frame.summary,
#                                *frame.successors
#                                ))


def load_frames(filename):
    f = open(filename)
    structlen = struct.calcsize(FRAME_STRUCTURE)
    frames = []
    for i in count():
        s = f.read(structlen)
        if not s:
            break
        frame = Frame(s)
        frame.index = i
        frames.append(frame)

    return frames



frames = link_frames(load_frames(INDEX_FILE))

save_frames(frames, INDEX_FILE + '-new')
save_frames_text(frames, TEXT_INDEX_FILE + '-new')
