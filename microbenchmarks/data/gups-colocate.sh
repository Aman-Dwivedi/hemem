#!/usr/bin/gnuplot
set terminal pdf size 8,2 fontscale 0.7
set output "gups-colocate.pdf"
#set xdata time
#set autoscale y
#file="/home/amanda/hemem-paper/figures/gups/hotset-move/mm-new.txt"
file1="data/colocate/gups/first-gups.txt"
file2="data/colocate/gups/second-gups.txt"
file3="data/colocate/gups/third-gups.txt"
file4="data/colocate/gups/fourth-gups.txt"
file5="data/colocate/gups/fifth-gups.txt"
file6="data/colocate/gups/sixth-gups.txt"
set datafile separator "\t"
#set xtics axis rangelimited
#set xtics scale 0.5 rotate by 25 offset -3,-0.5
#set arrow from 5, graph 0 to 5, graph 1 nohead
set ytics 0.002
#set key at 70, 0.024
#set key bottom right
set key outside top left horizontal font ",7"
set xrange [0:500]
set yrange [0:0.01]

set xlabel "Time (s)"
set ylabel "GUPS"

cpu_freq=2100000000
start=`awk '{ if(min == 0) { min = $1 } else { min = $1 < min ? $1 : min } } END { print min }' data/colocate/gups/*.txt`

#set title "GUPS Throughput"
plot \
  file1 using ($1-start)/cpu_freq:2 title "GUPS 1 (1.0)" with lines lw 4, \
  file2 using ($1-start)/cpu_freq:2 title "GUPS 2 (0.1)" with lines lw 4, \
  file3 using ($1-start)/cpu_freq:2 title "GUPS 3 (0.1)" with lines lw 4, \
  file4 using ($1-start)/cpu_freq:2 title "GUPS 4 (0.1)" with lines lw 4, \
  file5 using ($1-start)/cpu_freq:2 title "GUPS 5 (0.1)" with lines lw 4, \
  file6 using ($1-start)/cpu_freq:2 title "GUPS 6 (0.1)" with lines lw 4

