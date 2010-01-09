#!/bin/sh

config_file () {
    local NEW="$1.new"
    [ -f "$NEW" ] || { echo "$NEW does not exist!"; return; }

    if [ -f "$1" ] ; then
	if [ "$2" = "-rm" ] ; then
	    rm "$NEW"
	elif cmp -s "$1" "$NEW" ; then
	    rm "$NEW"
	else
	    echo
	    echo "You might want to check $NEW"
	    echo
	fi
    else
	mv $NEW $1
    fi
}

config_file /usr/share/get-comics/comics.xml
