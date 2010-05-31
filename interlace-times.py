#!/usr/bin/python
import sys, os
from itertools import izip

fn1, fn2 = sys.argv[1:3]

#use on logs created by
#
#make TEST_OPTIONS='timer=1 reload=dumpfiles/xx.dump' test
#
# ideally keeping the camera in position

f1 = open(fn1)
f2 = open(fn2)

print "%s  %s" % (fn1, fn2)
for l1, l2 in izip(f1, f2):
    s1, t1 = l1.strip().split()
    s2, t2 = l2.strip().split()
    ratio = float(t1) / float(t2)
    if ratio > 1.1:
        fmt = "%s  %6s    %2.3f >%6s"
    elif ratio < 1 / 1.1:
        fmt = "%s  %6s  < %2.3f  %6s"
    else:
        fmt = "%s  %6s    %2.3f  %6s"

    print fmt % (s1, t1, ratio, t2)
