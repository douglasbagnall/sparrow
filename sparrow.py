import struct
import numpy as np
import heapq
from itertools import count


INDEX_FILE = 'content/jpeg.index'
TEXT_INDEX_FILE = 'content/jpeg.index.txt'
FRAME_STRUCTURE = 'II48s8I'
BLOB_NAME='content/jpeg.blob'


def save_frames(frames, filename):
    f = open(filename, 'w')
    for frame in frames:
        f.write(struct.pack(FRAME_STRUCTURE,
                            frame.glob_index,
                            frame.jpeg_len,
                            frame.summary,
                            *frame.successors
                            ))


def save_frames_text(frames, filename):
    f = open(filename, 'w')
    for frame in frames:
        print >> f, ("Frame:")
        print >> f, (" index: %d" % frame.index)
        print >> f, (" glob_index: %d" % frame.glob_index)
        print >> f, (" jpeg_len: %d" % frame.jpeg_len)
        print >> f, (" summary: %s" % list(frame.summary))
        print >> f, (" successors: %s" % frame.successors)


class Frame:
    def __init__(self, packed=None):
        if packed is not None:
            data = struct.unpack(FRAME_STRUCTURE, packed)
            self.glob_index = data[0]
            self.jpeg_len = data[1]
            self.summary = data[2]
            self.array = np.fromstring(data[2], dtype=np.uint8)
            self.successors = list(data[3:])
            self.next = self.successors[0]



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
        if f.successors[0] == 0:
            f.head = heads[-1]
            tails.append(f)
            isbreak = True

    for f in heads + tails:
        if not hasattr(f, 'array'):
            f.array =  np.fromstring(f.summary, dtype=np.uint8)

    #print frames, heads, tails

    for t in tails:
        closest = [x[1] for x in heapq.nsmallest(7, distance_gen(t, heads), key=lambda x: x[0])]
        t.successors = [0] + [x.index for x in closest]
        print t.index, t.successors

    return frames
