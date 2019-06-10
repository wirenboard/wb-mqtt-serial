#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <fcntl.h>
#include <stdio.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <string>

#include "lls_device.h"

namespace {
    const int DefaultTimeoutMs = 1000;
    const int FrameTimeoutMs = 50;
}

REGISTER_BASIC_INT_PROTOCOL("lls", TLLSDevice, TRegisterTypes({{ 0, "default", "value", Float, true }}));

TLLSDevice::TLLSDevice(PDeviceConfig device_config, PPort port, PProtocol protocol)
    : TBasicProtocolSerialDevice<TBasicProtocol<TLLSDevice>>(device_config, port, protocol)
{}


static const int MAX_LEN = 100;


static unsigned char dallas_crc8(const unsigned char * data, const unsigned int size)
{
    unsigned char crc = 0;
    for ( unsigned int i = 0; i < size; ++i )
    {
        unsigned char inbyte = data[i];
        for ( unsigned char j = 0; j < 8; ++j )
        {
            unsigned char mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if ( mix ) crc ^= 0x8C;
            inbyte >>= 1;
        }
    }
    return crc;
}


uint64_t TLLSDevice::ReadRegister(PRegister reg)
{
    Port()->SkipNoise();
    Port()->CheckPortOpen();

    uint8_t cmd = (reg->Address & 0xFF00) >> 8;
    uint8_t offset = (reg->Address & 0x00FF);

    uint8_t buf[MAX_LEN] = {};
    buf[0] = 0x31;
    buf[1] = SlaveId;
    buf[2] = cmd;
    buf[3] = dallas_crc8(buf, 3);
    Port()->WriteBytes(buf, 4);

    int len = Port()->ReadFrame(buf, MAX_LEN, this->DeviceConfig()->FrameTimeout);
    if (buf[0] != 0x3e) {
        throw TSerialDeviceTransientErrorException("invalid response prefix");
    }
    if (buf[1] != SlaveId) {
        throw TSerialDeviceTransientErrorException("invalid response network address");
    }
    if (buf[2] != cmd) {
        throw TSerialDeviceTransientErrorException("invalid response cmd");
    }
    if (buf[len-1] != dallas_crc8(buf, len-1)) {
        throw TSerialDeviceTransientErrorException("invalid response crc");
    }

    int result_buf[4] = {};
    for (int i=0; i< reg->ByteWidth(); ++i) {
        result_buf[i] = buf[3+offset+i];
    }
    std::cout <<  int(result_buf[0]) << std::endl;
    
    uint32_t val = (uint32_t)0xFF00 & (int) result_buf[0];
    return val;
       
}

void TLLSDevice::WriteRegister(PRegister, uint64_t)
{
    throw TSerialDeviceException("LLS protocol: writing register is not supported");
}
