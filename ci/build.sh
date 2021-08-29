#!/bin/bash
#
# Simply run cmake and run make to produce firmwares.
#
set -e

echo "Starting build in `pwd`"
sudo chown -R dev:dev .

export HOME=/home/dev/
source "${IDF_PATH}"/export.sh

# Go, build
mkdir build-r1.0 && cd build-r1.0
rm ../sdkconfig || true
cmake .. -DCMAKE_BUILD_TYPE=Release -DHARDWARE_REVISION=1.0
make -j4
cd ..

mkdir build-r2.0 && cd build-r2.0
rm ../sdkconfig || true
cmake .. -DCMAKE_BUILD_TYPE=Release -DHARDWARE_REVISION=2.0
make -j4
cd ..

# Also compile unit tests
#cd unit-tests
#mkdir build && cd build
#cmake .. -DCMAKE_BUILD_TYPE=Release
#make -j4


echo "SUCCESS"
