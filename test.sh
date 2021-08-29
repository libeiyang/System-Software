#!/bin/bash

sleepexe()
{
    sleep $1
    ./sensor_node $2 $3 $4 $5
}
# * argv[1] = sensor ID
# * argv[2] = sleep time
# * argv[3] = server IP
# * argv[4] = server port
sensor()
{
    sleep 1
      sleepexe 1 129 1 127.0.0.1 1234 \
    & sleepexe 3 112 1 127.0.0.1 1234 \
    & sleepexe 1 142 1 127.0.0.1 1234 \
    & sleepexe 5 15 1 127.0.0.1 1234  \
    & sleepexe 1 37 1 127.0.0.1 1234  \
    & sleepexe 7 49 1 127.0.0.1 1234 \
    & sleepexe 8 21 1 127.0.0.1 1234 \
    & sleepexe 1 132 1 127.0.0.1 1234

}
valgrind --tool=memcheck --leak-check=full --leak-check=full --show-leak-kinds=all ./sensor_gateway 1234 & sensor
