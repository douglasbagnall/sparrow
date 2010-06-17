#!/usr/bin/python

import os, sys, subprocess
import random


JPEG_DIR = "/home/douglas/sparrow/content/jpg/"
ALL_MJPEG_DIR = "/home/douglas/sparrow/content/mjpeg/"
ALL_DF_MJPEG_DIR = "/home/douglas/sparrow/content/mjpeg_df/"
MJPEG_DIR = "/home/douglas/sparrow/content/selected_mjpeg/"
JPEG_NAME_TEMPLATE = "%03d-%%05d.jpg"


def split_mjpeg(src, dest):    
    cmd = ["ffmpeg", "-i", src,
           "-vcodec", "copy", dest,
           ]
    subprocess.check_call(cmd)

def split_directory(src_dir, dest_dir):
    for i, src in enumerate(os.listdir(src_dir)):
        dest = os.path.join(dest_dir, JPEG_NAME_TEMPLATE % i)
        split_mjpeg(os.path.join(src_dir, src), dest)


def random_selection(dest, sf=0.5, df=0.1):
    if os.listdir(dest):
        print "WARNING: %s is not empty" % (dest,)

    for f in os.listdir(ALL_MJPEG_DIR):
        if random.random() < sf:
            os.link(ALL_MJPEG_DIR + f, MJPEG_DIR + f)

    for f in os.listdir(ALL_DF_MJPEG_DIR):
        if random.random() < df:
            os.link(ALL_DF_MJPEG_DIR + f, MJPEG_DIR + 'df+' + f)


#random_selection(MJPEG_DIR, 0.35, 0.09)
split_directory(MJPEG_DIR, JPEG_DIR)            


