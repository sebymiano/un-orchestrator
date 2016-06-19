#!/bin/bash

BASE=`pwd`

# json-spirit
git clone https://github.com/sirikata/json-spirit
cd json-spirit/build
cmake .
make
./json_test
make install

# rofl
cd $BASE
git clone https://github.com/bisdn/rofl-common
cd rofl-common/
git checkout stable-0.6
./autogen.sh
cd build
../configure
make
make install

# inih
cd $BASE/contrib
unzip inih.zip
cd inih
cp * ../../orchestrator/node_resource_manager/database_manager/SQLite

