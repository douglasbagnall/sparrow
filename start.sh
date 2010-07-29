#!/bin/bash
#0 get in place

sleep 30

cd /home/douglas/sparrow
[[ "$DISPLAY" ]] || export DISPLAY=:0


#1 initialisation
#1.1 tune camera?

v4l2ctrl -l /home/douglas/sparrow.conf

#xrandr

#1.2 start up reverse ssh tunnel

#2 go
while true; do
    echo hello
    ./gtk-app -f --screens=2 --fps=20 -c
done
