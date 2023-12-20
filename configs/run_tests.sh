#!/bin/bash

source batsim_environment.sh

for m in {25,50,75,100}; 
do
    for i in {250,500,1000,2000}; 
    do
        for k in {2,3};
        echo "Running experiment paper_easybf${k}_${m}k_${i}"
        myBatchTasks.sh -f ../configs/${m}k_configs/paper_easybf${k}_${m}k_${i}.config -o paper_easybf${k}_${m}k_${i} -m charliecloud -p background -t 10
        echo "Finished experiment paper_easybf${k}_${m}k_${i}"
        sleep 300
        do
        done
    done
done