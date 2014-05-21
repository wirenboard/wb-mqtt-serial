#!/bin/sh
PORT=$1
ADDR=$2
#~ MODBUS_BIN="/home/boger/src/modbus-utils/mbClient"
MODBUS_BIN="modbus_client"
MODBUS_CMD="$MODBUS_BIN --debug -m rtu -s1 -pnone $PORT  -a$ADDR"

PORT_BASENAME=`basename $PORT`
DEVICE_ID="wb-icpdas-$PORT_BASENAME-$ADDR"
DEVICE_NAME="ICP-DAS $PORT $ADDR"

#~ MQTT_PARAMS="-h 192.168.0.181"
MQTT_PARAMS=""
MQTT_SUB="mosquitto_sub $MQTT_PARAMS"
MQTT_PUB="mosquitto_pub $MQTT_PARAMS"

handle_message()
{
    #~ echo sdf;
    $MODBUS_CMD -t0x05 -r$1 $2
};


register_channel()
{
    $MQTT_PUB -t "$1" -r -m "0"
    $MQTT_PUB -t "$1/meta/type" -r -m "switch"
    $MQTT_SUB -t "$1/on" | xargs -n 1 sh $0 $PORT $ADDR --internal message $2 &

};

wait_forever()
{
    while [ 1 ]; do
        sleep 1;
    done;
}

main()
{
    #FIXME: dirty hack vvv
    killall -9 mosquitto_sub


    $MQTT_PUB -t "/devices/$DEVICE_ID/meta/name" -r -m "$DEVICE_NAME"

    register_channel "/devices/$DEVICE_ID/controls/relay 1" 0
    register_channel "/devices/$DEVICE_ID/controls/relay 2" 1
    register_channel "/devices/$DEVICE_ID/controls/relay 3" 2
    trap 'kill $(jobs -p)' EXIT

    wait_forever
};


if [ "x$PORT" = "x" ]; then
    echo "Please specify port";
    exit 1;
fi

if [ "x$ADDR" = "x" ]; then
    echo "Please specify addr";
    exit 1;
fi





if [ "$3" = "--internal" ]; then
    if [ "$4" = "message" ]; then
        handle_message $5 $6
    fi

else
    main
fi


