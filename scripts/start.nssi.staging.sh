#!/bin/sh

export ADIOS_SRC_PATH=$HOME/projects/adios/src/adios

SERVICE_COUNT=$1 ; shift
SERVICE_CONFIG_FILE=$1 ; shift

aprun -n $SERVICE_COUNT -N 1 $ADIOS_SRC_PATH/src/nssi-staging-server $SERVICE_CONFIG_FILE