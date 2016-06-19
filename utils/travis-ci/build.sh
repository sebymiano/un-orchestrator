#!/bin/bash

BASE=`pwd`

# build name resolver
cd $BASE/name-resolver
cmake .
make

# build orchestrator
cd $BASE/orchestrator
cmake .
make
