#!/usr/bin/python3

# Simulates sending extra noise bytes after modbus response

import signal
import serial


def main():
    signal.signal(signal.SIGINT, signal.SIG_DFL)

    ser = serial.Serial('/dev/ttyRS485-2', 115200)
    while 1:
        ser.read(8)
        ser.write(b'\x01\x03\x02\x00\x03\xf8\x45')
        ser.read(8)
        ser.write(b'\x01\x03\x02\x00\x03\xf8\x45')
        ser.read(8)
        ser.write(b'\x01\x03\x02\x00\x14\xb8\x4b\xb0\xb1')
        ser.read(8)
        ser.write(b'\x01\x03\x02\x00\x14\xb8\x4b\xb0\xb1\xb2')
        ser.read(8)
        ser.write(b'\x01\x03\x02\x00\x14\xb8\x4b\xb0\xb1\xb2\xb3')
        ser.read(8)
        ser.write(b'\x01\x03\x02\x00\x14\xb8\x4b\xb0\xb1\xb2\xb3\xb4')
        ser.read(8)
        ser.write(b'\x01\x03\x02\x00\x14\xb8\x4b\xb0\xb1\xb2\xb3\xb4\xb5\x01\x03\x02\x00\x14\xb8\x4b\xb0\xb1\xb2\xb3\xb4\xb5\x01\x03\x02\x00\x14\xb8\x4b\xb0\xb1\xb2\xb3\xb4\xb5')
    ser.close() 


if __name__ == "__main__":
    main()
