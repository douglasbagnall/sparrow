#!/usr/bin/python

import os, sys, re
import subprocess
import array
import struct
import Image
from itertools import count

from sparrow import FRAME_STRUCTURE, INDEX_FILE, TEXT_INDEX_FILE, BLOB_NAME
from sparrow import save_frames, save_frames_text, link_frames
from sparrow import Frame

class Sequence:
    def __init__(self, seq_id):
        self.id = seq_id
        self.frames = []
        self.append = self.frames.append


def stretch(im, size, filter=Image.NEAREST):
    im.load()
    im = im._new(im.im.stretch(size, filter))
    return im

def downscale_pil(jpeg_data):
    p = subprocess.Popen(['djpeg', '-scale', '1/8', '-greyscale'],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    out, err = p.communicate(jpeg_data)
    # out will be scaled down by 8, ie from 800x600 to 100x75
    # we want it down to 8x6, 4x3 or thereabouts
    # precision doesn't matter
    header, imstring = out.split('255\n', 1)
    header = header.split()
    size = [int(x) for x in header[1:3]]
    im = Image.fromstring("L", size, imstring)
    im = stretch(im, (8, 6))
    return im.tostring()

def process_dir(dirname, blobname=BLOB_NAME):
    fn_re = re.compile(r'(\d{3})-(\d{5}).jpg')

    jpegblob = open(blobname, 'w')

    frames = []
    glob_index = 0

    files = sorted(x for x in os.listdir(dirname) if fn_re.match(x))

    seq_id = None
    frame_counter = count()
    for fn in files:
        f = open(os.path.join(dirname, fn))
        jpeg = f.read()
        f.close()
        jpegblob.write(jpeg)

        frame = Frame()
        frame.index = frame_counter.next()
        frame.glob_index = glob_index
        frame.jpeg_len = len(jpeg)
        frame.summary = downscale_pil(jpeg)
        frame.successors = [0] * 8

        m = fn_re.match(fn)
        new_seq_id = m.group(1)
        if new_seq_id != seq_id:
            seq_id = new_seq_id
        elif frames:
            prev = frames[-1]
            prev.successors[0] = frame.index

        frames.append(frame)
        glob_index += len(jpeg)


    save_frames(frames, INDEX_FILE + '-prelink')
    save_frames_text(frames, TEXT_INDEX_FILE + '-prelink')

    link_frames(frames)

    save_frames(frames, INDEX_FILE + '-post-link')
    save_frames_text(frames, TEXT_INDEX_FILE + '-post-link')




process_dir(sys.argv[1])
