#!/bin/bash

while [[ $( grep "source" $1 ) == "" ]]
do
        sleep 1
        #echo "Waiting for graph setup"
done

echo "Graph setup"
