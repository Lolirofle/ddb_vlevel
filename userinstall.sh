#!/bin/sh
/bin/mkdir -pv ${HOME}/.local/lib/deadbeef
if [ -f ./out/ddb_vlevel.so ]; then
	/usr/bin/install -v -c -m 644 ./out/ddb_vlevel.so ${HOME}/.local/lib/deadbeef/
else
	/usr/bin/install -v -c -m 644 ./ddb_vlevel.so ${HOME}/.local/lib/deadbeef/
fi
CHECK_PATHS="/usr/local/lib/deadbeef /usr/lib/deadbeef"
for path in $CHECK_PATHS; do
	if [ -d $path ]; then
		if [ -f $path/ddb_vlevel.so ]; then
			echo "Warning: Some version of the vlevel plugin is present in $path, you should remove it to avoid conflicts!"
		fi
	fi
done
