#!/bin/bash
#
# Simply run cmake and run make to produce firmwares.
#
set -e

echo "Starting build in `pwd`"
sudo chown -R dev:dev .

#export HOME=/home/dev/
#sudo chown -R dev:dev ~/.ssh/
#chmod 600 ~/.ssh/id_rsa

# Go, build
mkdir build && cd build
rm ../sdkconfig || true
cmake .. -DCMAKE_BUILD_TYPE=Release -DHARDWARE_REVISION=1.0
make -j4
cd ..

# Also compile unit tests
cd unit-tests
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4


echo "SUCCESS"
