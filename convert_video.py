#!/usr/bin/python

import os, sys, subprocess

SRC_DIR = "/home/douglas/sparrow/content/dv/late/"
DEST_DIR = "/home/douglas/sparrow/content/mjpeg/"
#for double frames
DF_DEST_DIR = "/home/douglas/sparrow/content/mjpeg_df/"

WIDTH = 800
HEIGHT = 600
CROP_RIGHT = 32
CROP_TOP = 24
PRE_CROP_RIGHT = 28
PRE_CROP_TOP = 21

CROP = False

def do_one(src, dest, df=False):
    if df:
        yadif_mode = 1
        fps_list = ['-fps', '50', '-ofps', '50']
    else:
        yadif_mode = 0
        fps_list = []

    if CROP:
        vf = ("yadif=%d,hqdn3d,scale=%d:%d,crop=%d:%d:%d:%d" %
              (yadif_mode, WIDTH + CROP_RIGHT, HEIGHT + CROP_TOP,
               WIDTH, HEIGHT, 0, CROP_TOP))
    else:
        vf = ("yadif=%d,hqdn3d,scale=%d:%d" %
              (yadif_mode, WIDTH, HEIGHT))


    cmd = ["mencoder",
           "-demuxer", "lavf",
           src,
           "-o", dest,
           "-nosound",
           "-ovc", "lavc",
           "-lavcopts", "vcodec=mjpeg",
           "-vf", vf,
           ] + fps_list

    subprocess.check_call(cmd)

if 0:
    do_one(SRC_DIR + "sparrow-1-3-4-12-14145.dv",
           DEST_DIR + "sparrow-1-3-4-12-14145-precrop.avi")
    sys.exit()

def go(df=False):
    if df:
        destdir = DF_DEST_DIR
    else:
        destdir = DEST_DIR
    for src in os.listdir(SRC_DIR):
        dest = destdir + src + '-late.avi'
        src = SRC_DIR + src
        do_one(src, dest, df)


#go(df=False)
go(df=True)



#hqdn3d=4:3:6 [:6*3/4  == 4]

