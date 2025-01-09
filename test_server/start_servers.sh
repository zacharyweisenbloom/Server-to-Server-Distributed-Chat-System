#!/usr/bin/bash

#uncomment the topolgy you want. The simple two-server topology is uncommented here.

# Change the SERVER variable below to point your server executable.
SERVER=./server

SERVER_NAME=`echo $SERVER | sed 's#.*/\(.*\)#\1#g'`

# Generate a simple two-server topology
#SERVER localhost 4050 localhost 4051 &
#SERVER localhost 4051 localhost 4050 & 

# Generate a capital-H shaped topology
$SERVER localhost 4050 localhost 4051 &
$SERVER localhost 4051 localhost 4050 localhost 4052 localhost 4053 &
$SERVER localhost 4052 localhost 4051 & 
$SERVER localhost 4053 localhost 4051 localhost 4055 &
$SERVER localhost 4054 localhost 4055 &
$SERVER localhost 4055 localhost 4054 localhost 4053 localhost 4056 &
$SERVER localhost 4056 localhost 4055 &

# Generate a 3x3 grid topology
#$SERVER localhost 8100 localhost 8101 localhost 8103 &
#$SERVER localhost 8101 localhost 8100 localhost 8102 localhost 8104 &
#$SERVER localhost 8102 localhost 8101 localhost 8105 &
#$SERVER localhost 8103 localhost 8100 localhost 8104 localhost 8106 &
#$SERVER localhost 8104 localhost 8101 localhost 8103 localhost 8105 localhost 8107 &
#$SERVER localhost 8105 localhost 8102 localhost 8104 localhost 8108 &
#$SERVER localhost 8106 localhost 8103 localhost 8107 &
#$SERVER localhost 8107 localhost 8106 localhost 8104 localhost 8108 &
#$SERVER localhost 8108 localhost 8105 localhost 8107 &


echo "Press ENTER to quit"
read
pkill $SERVER_NAME
