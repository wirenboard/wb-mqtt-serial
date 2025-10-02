#!/usr/bin/env bash

CONFFILE=/etc/wb-mqtt-serial.conf

[ -f "$CONFFILE" ] && exit 0

. /usr/lib/wb-utils/wb_env.sh

wb_source "of"

if of_machine_match "wirenboard,wirenboard-85x"; then
    BOARD_CONF="/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.wb85"
elif of_machine_match "wirenboard,wirenboard-8xx"; then
    BOARD_CONF="/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.wb8"
elif of_machine_match "wirenboard,wirenboard-720"; then
    BOARD_CONF="/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.wb7"
elif of_machine_match "contactless,imx6ul-wirenboard670"; then
    BOARD_CONF="/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.wb67"
elif of_machine_match "contactless,imx6ul-wirenboard60"; then
    BOARD_CONF="/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.wb6"
elif of_machine_match "contactless,imx28-wirenboard50"; then
    BOARD_CONF="/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.wb5"
elif of_machine_match "contactless,imx23-wirenboard41" ||
     of_machine_match "contactless,imx23-wirenboard32" ||
     of_machine_match "contactless,imx23-wirenboard28";
then
    BOARD_CONF="/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.wb234"
else
    BOARD_CONF="/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.default"
fi

cp "$BOARD_CONF" "$CONFFILE"
