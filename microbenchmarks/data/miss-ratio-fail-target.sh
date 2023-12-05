#!/usr/bin/gnuplot
set terminal pdf size 8,2 fontscale 0.7
set output "miss-ratio-fail-target.pdf"
#set xdata time
#set autoscale y
file1="data/fail-target/logs/first-log.txt"
file2="data/fail-target/logs/second-log.txt"
file3="data/fail-target/logs/third-log.txt"
file4="data/fail-target/logs/fourth-log.txt"
file5="data/fail-target/logs/fifth-log.txt"
set datafile separator "\t"
set ytics 0.25
set key outside top left horizontal font ",7"
set xrange [0:500]
set yrange [0:1]

set xlabel "Time (s)"
set ylabel "Miss Ratio"

load "data/fail-target-marks.gnuplot"

cpu_freq=2100000000
start=`awk '{ if(min == 0) { min = $1 } else { min = $1 < min ? $1 : min } } END { print min }' data/fail-target/logs/*.txt`

plot \
  file1 using ($1-start)/cpu_freq:2 title "GUPS 1 (0.1)" with lines lw 4, \
  file2 using ($1-start)/cpu_freq:2 title "GUPS 2 (0.1)" with lines lw 4, \
  file3 using ($1-start)/cpu_freq:2 title "GUPS 3 (0.1)" with lines lw 4, \
  file4 using ($1-start)/cpu_freq:2 title "GUPS 4 (0.1)" with lines lw 4, \
  file5 using ($1-start)/cpu_freq:2 title "GUPS 5 (0.1)" with lines lw 4
