#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "s2k_device.h"

namespace
{
    const TRegisterTypes RegisterTypes{{TS2KDevice::REG_RELAY, "relay", "switch", U8},
                                       {TS2KDevice::REG_RELAY_MODE, "relay_mode", "value", U8, true},
                                       {TS2KDevice::REG_RELAY_DEFAULT, "relay_default", "value", U8, true},
                                       {TS2KDevice::REG_RELAY_DELAY, "relay_delay", "value", U8, true}};
}

void TS2KDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(
        new TUint32SlaveIdProtocol("s2k", RegisterTypes),
        new TBasicDeviceFactory<TS2KDevice>("#/definitions/simple_device_with_setup", "#/definitions/common_channel"));
}

TS2KDevice::TS2KDevice(PDeviceConfig config, PProtocol protocol)
    : TSerialDevice(config, protocol),
      TUInt32SlaveId(config->SlaveId)
{
    RelayState[1] = 2;
    RelayState[2] = 2;
    RelayState[3] = 2;
    RelayState[4] = 2;
}

// clang-format off
uint8_t TS2KDevice::CrcTable[] = {
        0x00,0x5E,0xBC,0xE2,0x61,0x3F,0xDD,0x83,0xC2,0x9C,0x7E,0x20,0xA3,0xFD,
        0x1F,0x41,0x9D,0xC3,0x21,0x7F,0xFC,0xA2,0x40,0x1E,0x5F,0x01,0xE3,0xBD,
        0x3E,0x60,0x82,0xDC,0x23,0x7D,0x9F,0xC1,0x42,0x1C,0xFE,0xA0,0xE1,0xBF,
        0x5D,0x03,0x80,0xDE,0x3C,0x62,0xBE,0xE0,0x02,0x5C,0xDF,0x81,0x63,0x3D,
        0x7C,0x22,0xC0,0x9E,0x1D,0x43,0xA1,0xFF,0x46,0x18,0xFA,0xA4,0x27,0x79,
        0x9B,0xC5,0x84,0xDA,0x38,0x66,0xE5,0xBB,0x59,0x07,0xDB,0x85,0x67,0x39,
        0xBA,0xE4,0x06,0x58,0x19,0x47,0xA5,0xFB,0x78,0x26,0xC4,0x9A,0x65,0x3B,
        0xD9,0x87,0x04,0x5A,0xB8,0xE6,0xA7,0xF9,0x1B,0x45,0xC6,0x98,0x7A,0x24,
        0xF8,0xA6,0x44,0x1A,0x99,0xC7,0x25,0x7B,0x3A,0x64,0x86,0xD8,0x5B,0x05,
        0xE7,0xB9,0x8C,0xD2,0x30,0x6E,0xED,0xB3,0x51,0x0F,0x4E,0x10,0xF2,0xAC,
        0x2F,0x71,0x93,0xCD,0x11,0x4F,0xAD,0xF3,0x70,0x2E,0xCC,0x92,0xD3,0x8D,
        0x6F,0x31,0xB2,0xEC,0x0E,0x50,0xAF,0xF1,0x13,0x4D,0xCE,0x90,0x72,0x2C,
        0x6D,0x33,0xD1,0x8F,0x0C,0x52,0xB0,0xEE,0x32,0x6C,0x8E,0xD0,0x53,0x0D,
        0xEF,0xB1,0xF0,0xAE,0x4C,0x12,0x91,0xCF,0x2D,0x73,0xCA,0x94,0x76,0x28,
        0xAB,0xF5,0x17,0x49,0x08,0x56,0xB4,0xEA,0x69,0x37,0xD5,0x8B,0x57,0x09,
        0xEB,0xB5,0x36,0x68,0x8A,0xD4,0x95,0xCB,0x29,0x77,0xF4,0xAA,0x48,0x16,
        0xE9,0xB7,0x55,0x0B,0x88,0xD6,0x34,0x6A,0x2B,0x75,0x97,0xC9,0x4A,0x14,
        0xF6,0xA8,0x74,0x2A,0xC8,0x96,0x15,0x4B,0xA9,0xF6,0xB6,0xFC,0x0A,0x54,
        0xD7,0x89,0x6B,0x35
};
// clang-format on

uint8_t TS2KDevice::CrcS2K(const uint8_t* array, int size)
{
    uint8_t crc = 0;
    for (int i = 0; i < size; i++) {
        crc = CrcTable[crc ^ array[i]];
    }
    return crc;
}

void TS2KDevice::WriteRegisterImpl(TPort& port, const TRegisterConfig& reg, const TRegisterValue& value)
{
    auto addr = GetUint32RegisterAddress(reg.GetAddress());
    if (reg.Type != REG_RELAY) {
        throw TSerialDeviceException("S2K protocol: invalid register for writing");
    }

    if (addr < 1 || addr > 4) {
        throw TSerialDeviceException("S2K protocol: invalid register address");
    }

    port.CheckPortOpen();
    uint8_t command[7] = {/* Address = */ (uint8_t)SlaveId,
                          /* Command length = */ 0x06,
                          /* Key = */ 0x00,
                          /* Command = */ 0x15, /* Relay control */
                          /* Relay No = */ (uint8_t)addr,
                          /* Relay program = */ (uint8_t)(value.Get<uint64_t>() ? 0x1 /* ON */ : 0x2 /* OFF */),
                          /* CRC placeholder */ 0x0};
    command[6] = CrcS2K(command, 6);
    port.WriteBytes(command, 7);
    uint8_t response[256];
    int size = port.ReadFrame(response, 256, GetResponseTimeout(port), GetFrameTimeout(port)).Count;
    if (size != 6 || response[0] != (uint8_t)SlaveId || response[1] != 5 || response[2] != 0x16) {
        throw TSerialDeviceTransientErrorException("incorrect response for 0x15 command");
    }
    if (response[5] != CrcS2K(response, 5)) {
        throw TSerialDeviceTransientErrorException("bad CRC for 0x15 command");
    }
    RelayState[response[3]] = response[4];
}

TRegisterValue TS2KDevice::ReadRegisterImpl(TPort& port, const TRegisterConfig& reg)
{
    auto addr = GetUint32RegisterAddress(reg.GetAddress());
    /* We have no way to get current relay state from device. Thats why we save last
       successful write to relay register and return it when regiter is read */
    switch (reg.Type) {
        case REG_RELAY:
            return TRegisterValue{RelayState[addr] != 0 && RelayState[addr] != 2};
        case REG_RELAY_MODE:
            return TRegisterValue{RelayState[addr]};
        case REG_RELAY_DEFAULT:
        case REG_RELAY_DELAY: {
            port.CheckPortOpen();
            /* Default state of relays is stored in configs 1-4,
               Default delay for relays - in configs 5-8 */
            uint8_t command[7] = {/* Address = */ (uint8_t)SlaveId,
                                  /* Command length = */ 0x06,
                                  /* Key = */ 0x00,
                                  /* Command = */ 0x05, /* Read configuration */
                                  /* Config No = */ (uint8_t)(addr + (reg.Type == REG_RELAY_DELAY ? 4 : 0)),
                                  /* Unused */ 0x0,
                                  /* CRC placeholder */ 0x0};
            command[6] = CrcS2K(command, 6);
            port.WriteBytes(command, 7);
            uint8_t response[256];
            int size = port.ReadFrame(response, 256, GetResponseTimeout(port), GetFrameTimeout(port)).Count;
            if (size != 6 || response[0] != (uint8_t)SlaveId || response[1] != 0x5 || response[2] != 0x6) {
                throw TSerialDeviceTransientErrorException("incorrect response for 0x5 command");
            }
            if (response[5] != CrcS2K(response, 5)) {
                throw TSerialDeviceTransientErrorException("bad CRC for 0x5 command");
            }
            return TRegisterValue{response[4]};
        }
        default:
            throw TSerialDeviceException("S2K protocol: invalid register for reading");
    }
}
