#!/usr/bin/python

import os, sys, re
import subprocess
import array
import struct
import Image
from cStringIO import StringIO

from sparrow import FRAME_STRUCTURE, INDEX_FILE, TEXT_INDEX_FILE
from sparrow import save_frames, save_frames_text


class Sequence:
    def __init__(self, seq_id):
        self.id = seq_id
        self.frames = []
        self.append = self.frames.append


class Frame:
    def __init__(self):
        pass

def downscale_manual(jpeg_data):
    #XXX maybe PIL would just be quicker!
    p = subprocess.Popen(['djpeg', '-scale', '1/8', '-greyscale'],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    out, err = p.communicate(jpeg_data)
    # out will be scaled down by 8, ie from 800x600 to 100x75
    # we want it down to 8x6, 4x3 or thereabouts
    # precision doesn't matter
    TW = 8
    TH = 6 # targets
    im = out.split('255\n', 1)[1]
    im_array = array.array('B', im)

    width = int((len(im) * 4 / 3) ** 0.5) #assume 4x3 (could read header!)
    height = width * 3 / 4
    start = height / (TH * 2) + width / (TW * 2)
    hstep = width / TW
    vstep = height * width / TH
    small = []

    for y in range(TH):
        point = start + y * vstep
        for x in range(TW):
            total = (im_array[point - 2] +
                     im_array[point + 2] +
                     im_array[point + 2 * width] +
                     im_array[point - 2 * width])
            small.append(total >> 2)
            point += hstep

    return small


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

def process_dir(dirname):
    os.chdir(dirname)

    fn_re = re.compile(r'(\d{3})-(\d{5}).jpg')
    jpeg_glob = open('jpeg.glob', 'w')
    sequences = []
    frames = []
    glob_index = 0

    files = sorted(x for x in os.listdir('.') if fn_re.match(x))
    seq = Sequence(None)
    count = 0
    for fn in files:
        m = fn_re.match(fn)
        #print fn
        new_seq_id = int(m.group(1))
        if new_seq_id != seq.id:
            seq = Sequence(new_seq_id)
            sequences.append(seq)

        f = open(fn)
        jpeg = f.read()
        f.close()
        jpeg_glob.write(jpeg)
        frame = Frame()
        frame.index = count
        frame.glob_index = glob_index
        frame.jpeg_len = len(jpeg)
        frame.summary = downscale_pil(jpeg)
        seq.frames.append(frame)
        frames.append(frame)
        frame.successors = [0] * 8
        if seq.frames:
            seq.frames[-1].successors[0] = count

        glob_index += len(jpeg)
        count += 1

    save_frames(INDEX_FILE)
    save_frames_text(TEXT_INDEX_FILE)




process_dir(sys.argv[1])
