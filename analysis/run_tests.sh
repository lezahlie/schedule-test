#!/bin/bash

for i in {25,50,75,100}; 
do
    echo "Running experiment paper_easybf3_${i}k"
    ./myBatchTasks.sh -f ../configs/paper_easybf3_${i}k.config  -o paper_easybf3_${i}k -m bare-metal -p background --tasks-per-node 10
    echo "Completed experiment paper_easybf3_${i}k"
    sleep 120
done
