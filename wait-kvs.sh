#!/bin/bash

while [[ $( grep "Preloading completed" $1 ) == "" ]]
do
        sleep 1
        #echo "Waiting for graph setup"
done

echo "FlexKVS setup"
