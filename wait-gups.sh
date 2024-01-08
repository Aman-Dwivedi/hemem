#!/bin/bash

while [[ $( grep -o "Initialization" $1 ) == "" ]]
do
        sleep 1
        #echo "Waiting for graph setup"
done

echo "GUPS Ready"
