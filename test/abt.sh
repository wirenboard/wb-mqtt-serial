#!/bin/bash
set -u -e
data_dir="$(dirname "$0")"

usage() {
    echo "usage: $0 show|accept" 1>&2
    exit 1
}

show() {
    diff -uN "$1" "$2" ||
    if [ $? = 2 ]; then
        echo "DIFF FAILED" 1>&2
        exit 1
    fi
}

accept() {
    echo "ACCEPT: $1" 1>& 2
    mv "$2" "$1"
}

process_files() {
    found=
    for out_filename in "$data_dir"/*.dat.out; do
        if [ -e "$out_filename" ]; then
            found=y
            filename="$(echo "$out_filename" | sed 's/\.out//g')"
            "$1" "$filename" "$out_filename"
        fi
    done
    if [ -z "$found" ]; then
        echo "no diffs found" 1>& 2
    fi
}

if [ $# -lt 1 ]; then
    usage
fi

case "$1" in
    show)
        process_files show
        ;;
    accept)
        process_files accept
        ;;
    *)
        usage
esac
