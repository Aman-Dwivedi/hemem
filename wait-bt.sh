#!/bin/bash

while [[ $( grep "Time" $1 ) == "" ]]
do
        sleep 1
        #echo "Waiting for graph setup"
done

echo "BT Ready"
