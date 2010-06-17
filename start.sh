#!/bin/bash
#0 get in place

cd /home/douglas/sparrow
[[ "$DISPLAY" ]] || $export DISPLAY=:0


#1 initialisation
#1.1 tune camera?


#1.2 start up reverse ssh tunnel

#2 go

./gtk-app -f --screens=2 -fps=20 --serial-calibration

