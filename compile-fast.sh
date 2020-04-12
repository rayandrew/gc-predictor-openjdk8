#!/bin/bash
##################################################
## Make sure that you installed the packages
## described here:
## https://github.com/hgomez/obuildfactory/wiki/How-to-build-and-package-OpenJDK-8-on-Linux
##################################################
## vars
SRC_FOLDER="sources/openjdk8"
CURRENT_FOLDER="$PWD"
###################################################
## in here, we just cd, configure and build
cd "$SRC_FOLDER"
bash configure 
make all
echo "Done, use java/javac located at build/linux-x86_64-normal-server-release/jdk/bin"
cd "$CURRENT_FOLDER"
##################################################
## done
