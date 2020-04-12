#!/bin/bash
##################################################
## Make sure that you installed the packages
## described here:
## https://github.com/hgomez/obuildfactory/wiki/How-to-build-and-package-OpenJDK-8-on-Linux
##################################################
## vars
JAVA_OUTPUT="j2sdk-image"
GC="java8-gc"
FOLDER="OBF_DROP_DIR/openjdk8"
###################################################
## Do the hustle
##################################################
## delete the old folders...
if [ -d "$JAVA_OUTPUT" ]; then
        rm -rf $JAVA_OUTPUT
fi
if [ -d "$GC" ]; then
        rm -rf $GC
fi
## find the file name, we want the j2sdk. The output
## folder contains just one file matching that name, 
## so ill make this simple...
FILE=$(ls "$FOLDER" | grep j2sdk)
## some sanity
if [ -z "$FILE" ]; then
	echo "File <$FILE> was not found, exiting..."
	exit
fi
echo "Uncompressing <$FILE> to <$PWD>"
tar xvjf $FOLDER/$FILE
## now, just rename the folder
mv $JAVA_OUTPUT $GC
## done
