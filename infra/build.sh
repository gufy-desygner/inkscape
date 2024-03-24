#!/bin/bash -e

mkdir -p build
cd build
cmake ..
make -j10
make install -j10
