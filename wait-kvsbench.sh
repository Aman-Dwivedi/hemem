#!/bin/bash

sleep 5

while [[ $( grep "Start" $1 ) == "" ]]
do
        sleep 1
        #echo "Waiting for graph setup"
done

echo "kvsbench Ready"
echo `pidof kvsbench`:0.05 > /tmp/miss_ratio_update
kill -s USR2 `pidof central-manager`
