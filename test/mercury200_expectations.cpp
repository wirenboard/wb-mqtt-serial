#include "mercury200_expectations.h"

void TMercury200Expectations::EnqueueMercury200EnergyResponse()
{
    Expector()->Expect(
        {
            0x00, // addr
            0x01, // addr
            0xe2, // addr
            0x40, // addr
            0x27,
            0xf4,
            0x10
        },
        {
            0x00, 0x01, 0xe2, 0x40, 0x27, // request
            0x00, 0x06, 0x21, 0x42, // tariff1
            0x00, 0x02, 0x08, 0x34, // tariff2
            0x00, 0x01, 0x11, 0x11, // tariff3
            0x00, 0x02, 0x22, 0x22, // tariff4
            0x32, 0x042 //crc
        }, __func__);
}

void TMercury200Expectations::EnqueueMercury200ParamResponse()
{
    Expector()->Expect(
        {
            0x00, // addr
            0x01, // addr
            0xe2, // addr
            0x40, // addr
            0x63,
            0xf4,
            0x23
        },
        {
            0x00, 0x01, 0xe2, 0x40, 0x63, // request
            0x12, 0x34, // Voltage
            0x56, 0x78, // Current
            0x76, 0x54, 0x32, // Power
            0x8a, 0x8a //crc
        }, __func__);
}

void TMercury200Expectations::EnqueueMercury200BatteryVoltageResponse()
{
    Expector()->Expect(
        {
            0x00, // addr
            0x01, // addr
            0xe2, // addr
            0x40, // addr
            0x29, // read battery Voltage
            0x75,
            0xD4
        },
        {
            0x00, 0x01, 0xe2, 0x40, 0x29, // addr + cmd
            0x03, 0x91, // Voltage
            0xe7, 0x93 //crc
        }, __func__);
}
