#!/bin/bash

BASE=`pwd`

# json-spirit
git clone https://github.com/sirikata/json-spirit
cd json-spirit/build
cmake . && make -j && sudo make install
if [ $? -ne 0 ]; then
	exit 1
fi


# rofl
cd $BASE
git clone https://github.com/bisdn/rofl-common
cd rofl-common/
git checkout stable-0.6
./autogen.sh
cd build
../configure && make -j && sudo make install
if [ $? -ne 0 ]; then
	exit 1
fi

# inih
cd $BASE/contrib
unzip inih.zip
cd inih
cp * ../../orchestrator/node_resource_manager/database_manager/SQLite

# microhttp
cd $BASE/
wget http://ftp.gnu.org/gnu/libmicrohttpd/libmicrohttpd-0.9.44.tar.gz
tar -xvf libmicrohttpd-0.9.44.tar.gz
cd libmicrohttpd-0.9.44
./configure && make
sudo make install && sudo ldconfig
