#!/bin/sh

cat $2 > $1
if [ $? -ne 0 ]; then
	echo "Failed to write image '$2' to device '$1'."
	exit 1
fi

sync
