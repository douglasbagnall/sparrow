#!/usr/bin/python

import os, sys
import struct
import os, sys, re
import numpy as np
import heapq
from itertools import count

from sparrow import FRAME_STRUCTURE, INDEX_FILE, TEXT_INDEX_FILE
from sparrow import save_frames, save_frames_text

#            f.write(struct.pack('II48s8I',
#                                frame.glob_index,
#                                frame.jpeg_len,
#                                frame.summary,
#                                *frame.successors
#                                ))


class Frame:
    def __init__(self, packed=None):
        if packed != None:
            data = struct.unpack(FRAME_STRUCTURE, packed)
            self.glob_index = data[0]
            self.jpeg_len = data[1]
            self.summary = data[2]
            self.array = np.fromstring(data[2], dtype=np.uint8)
            self.successors = list(data[3:])
            self.next = self.successors[0]


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


def distance_gen(tail, heads):
    s = tail.array
    for h in heads:
        if h is not tail.head:
            yield((sum((s - h.array) ** 2), h))


def link_frames(frames):
    tails = []
    heads = []
    isbreak = True
    for f in frames:
        if isbreak:
            heads.append(f)
            isbreak = False
        if f.next == 0:
            f.head = heads[-1]
            tails.append(f)
            isbreak = True

    print frames, heads, tails

    for t in tails:
        closest = [x[1] for x in heapq.nsmallest(7, distance_gen(t, heads), key=lambda x: x[0])]
        t.successors = [0] + [x.index for x in closest]
        print t.successors

    return frames


frames = link_frames(load_frames(INDEX_FILE))

save_frames(frames, INDEX_FILE + '-new')
save_frames_text(frames, TEXT_INDEX_FILE + '-new')
