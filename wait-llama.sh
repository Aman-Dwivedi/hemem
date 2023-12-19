#!/bin/bash

while [[ $( grep "The key to happiness in one short sentence is:" $1 ) -eq "" ]]
do
        sleep 1
        #echo "Waiting for graph setup"
done

echo "LLaMa Ready"

