#!/bin/bash

SENDWINDOW=(64 32 16 8)
DATA_TIMER_TIMEOUT=(1500 2000 2500 3000)
ACK_TIMER_TIMEOUT=(200 500 1000)

for sw in ${SENDWINDOW[@]};
do
    for dtt in ${DATA_TIMER_TIMEOUT[@]};
    do
        for att in ${ACK_TIMER_TIMEOUT[@]};
        do
            rm -rf mydatalink.h
            cp mydatalink.h.sp mydatalink.h
            sed -i "s/_SW_/$sw/g"   mydatalink.h
            sed -i "s/_DTT_/$dtt/g" mydatalink.h
            sed -i "s/_ATT_/$att/g" mydatalink.h
            gcc *.c -Ofast -o opt -lm

            screen -L -Logfile 1-A.log -dmS A ./opt -n -p 59144 -u A 
            screen -L -Logfile 1-B.log -dmS B ./opt -n -p 59144 -u B 

            screen -L -Logfile 2-A.log -dmS A  ./opt -n -p 59145  A 
            screen -L -Logfile 2-B.log -dmS B  ./opt -n -p 59145  B

            screen -L -Logfile 3-A.log -dmS A  ./opt -n -p 59146 -u -f A 
            screen -L -Logfile 3-B.log -dmS B  ./opt -n -p 59146 -u -f B

            sleep 900
            kill -15  $(ps -x|grep SCREEN|awk '{print $1}')

            screen -L -Logfile 4-A.log -dmS A  ./opt -n -p 59147 -f A
            screen -L -Logfile 4-B.log -dmS B  ./opt -n -p 59147 -f B

            screen -L -Logfile 5-A.log -dmS A  ./opt -n -p 59148 -f --ber=1e-4 A
            screen -L -Logfile 5-B.log -dmS B  ./opt -n -p 59148 -f --ber=1e-4 B

            sleep 900
            kill -15  $(ps -x|grep SCREEN|awk '{print $1}')

            mkdir "$sw-$dtt-$att"
            mv *.log "$sw-$dtt-$att"
        done
    done
done
