#!/bin/bash

sleep 5

while [[ $( grep "Start" $1 ) == "" ]]
do
        sleep 1
        #echo "Waiting for graph setup"
done

echo "kvsbench Ready"
