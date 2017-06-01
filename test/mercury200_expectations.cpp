#include "mercury200_expectations.h"

void TMercury200Expectations::EnqueueMercury200EnergyResponse()
{
    Expector()->Expect(
        {
            0x01, // addr
            0x56, // addr
            0x10, // addr
            0x1e, // addr
            0x27,
            0x40,
            0x37
        },
        {
            0x01, 0x56, 0x10, 0x1e, 0x27, // request
            0x00, 0x06, 0x21, 0x42, // tariff1
            0x00, 0x02, 0x08, 0x34, // tariff2
            0x00, 0x01, 0x11, 0x11, // tariff3
            0x00, 0x02, 0x22, 0x22, // tariff4
            0x22, 0x2f //crc
        }, __func__);
}

void TMercury200Expectations::EnqueueMercury200ParamResponse()
{
    Expector()->Expect(
        {
            0x01, // addr
            0x56, // addr
            0x10, // addr
            0x1e, // addr
            0x63,
            0x40,
            0x04
        },
        {
            0x01, 0x56, 0x10, 0x1e, 0x63, // request
            0x12, 0x34, // Voltage
            0x56, 0x78, // Current
            0x76, 0x54, 0x32, // Power
            0x78, 0x56 //crc
        }, __func__);
}

void TMercury200Expectations::EnqueueMercury200BatteryVoltageResponse()
{
    Expector()->Expect(
        {
            0x01, // addr
            0x56, // addr
            0x10, // addr
            0x1e, // addr
            0x29, // read battery Voltage
            0xc1,
            0xf3
        },
        {
            0x01, 0x56, 0x10, 0x1e, 0x29, // addr + cmd
            0x03, 0x91, // Voltage
            0xd0, 0x89 //crc
        }, __func__);
}
