import struct


INDEX_FILE = 'content/jpg/jpeg.index'
TEXT_INDEX_FILE = 'content/jpg/jpeg.index.txt'
FRAME_STRUCTURE = 'II48s8I'

def save_frames(frames, filename):
    f = open(filename, 'w')
    for frame in frames:
        print (FRAME_STRUCTURE,
                            frame.glob_index,
                            frame.jpeg_len,
                            frame.summary,
                            frame.successors
                            )
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

