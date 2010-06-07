#!/usr/bin/python

import os, sys, subprocess

SRC_DIR = "/home/douglas/sparrow/content/dv/"
DEST_DIR = "/home/douglas/sparrow/content/mjpeg_df/"

WIDTH = 800
HEIGHT = 600
CROP_RIGHT = 32
CROP_TOP = 24
PRE_CROP_RIGHT = 28
PRE_CROP_TOP = 21

def do_one(src, dest):
    cmd = ["mencoder",
           "-demuxer", "lavf",
           src,
           "-o", dest,
           "-nosound",
           "-ovc", "lavc",
           "-lavcopts", "vcodec=mjpeg",
           "-vf", ("yadif=1,hqdn3d,scale=%d:%d,crop=%d:%d:%d:%d" %
                   (WIDTH + CROP_RIGHT, HEIGHT + CROP_TOP,
                    WIDTH, HEIGHT, 0, CROP_TOP)),
           '-fps', '50',
           '-ofps', '50',
           ]

    subprocess.check_call(cmd)

if 0:
    do_one(SRC_DIR + "sparrow-1-3-4-12-14145.dv",
           DEST_DIR + "sparrow-1-3-4-12-14145-precrop.avi")
    sys.exit()

def go():
    for src in os.listdir(SRC_DIR):
        dest = DEST_DIR + src + '.avi'
        src = SRC_DIR + src
        do_one(src, dest)


go()

#DEST=sparrow-test.avi

#SCALE=scale=1024:768

#REFERENCE_FILTERS=yadif,hqdn3d
#FILTERS=yadif,hqdn3d,$SCALE

#FILTERS="yadif=1:1,mcdeint=2:1:10,$SCALE"

#FILTERS=yadif,hqdn3d,eq2=1.2:1.0:0.0:1.0:1.0:1.0:0.8:1.0,$SCALE
#pp=tn/al:f
#hqdn3d=4:3:6 [:6*3/4]
#luma_spatial:chroma_spatial:luma_tmp:chroma_tmp
#ow=8:1.0:1.0
#
#eq2[=gamma:contrast:brightness:saturation:rg:gg:bg:weight]
#2xsai

#FPS=' -fps 25 -ofps 50'
#mencoder $SRC -o $(basename $SRC .dv).avi -nosound -ovc lavc -lavcopts vcodec=mjpeg -vf $FILTERS $FPS


