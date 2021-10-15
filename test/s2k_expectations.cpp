#include "s2k_expectations.h"

void TS2KDeviceExpectations::EnqueueSetRelayOnResponse()
{
    Expector()->Expect(
        {
            0x01, // address
            0x06, // size
            0x00, // key
            0x15, // command (0x15 - relay control)
            0x01, // relay No
            0x01, // relay ON
            0x4e  // sum
        },
        {
            0x01, // address
            0x05, // size
            0x16, // command (0x16 - reply for relay control)
            0x01, // relay No
            0x01, // relay ON
            0x4d  // sum
        },
        __func__);
}

void TS2KDeviceExpectations::EnqueueSetRelay2On()
{
    Expector()->Expect(
        {
            0x01, // address
            0x06, // size
            0x00, // key
            0x15, // command (0x15 - relay control)
            0x02, // relay No
            0x01, // relay ON
            0x1b  // sum
        },
        {
            0x01, // address
            0x05, // size
            0x16, // command (0x16 - reply for relay control)
            0x02, // relay No
            0x01, // relay ON
            0x18  // sum
        },
        __func__);
}

void TS2KDeviceExpectations::EnqueueSetRelay3On2()
{
    Expector()->Expect(
        {
            0x01, // address
            0x06, // size
            0x00, // key
            0x15, // command (0x15 - relay control)
            0x03, // relay No
            0x01, // relay ON
            0xdf  // sum
        },
        {
            0x01, // address
            0x05, // size
            0x16, // command (0x16 - reply for relay control)
            0x03, // relay No
            0x01, // relay ON
            0xdc  // sum
        },
        __func__);
}

void TS2KDeviceExpectations::EnqueueSetRelayOffResponse()
{
    Expector()->Expect(
        {
            0x01, // address
            0x06, // size
            0x00, // key
            0x15, // command (0x15 - relay control)
            0x01, // relay No
            0x02, // relay OFF
            0xac  // sum
        },
        {
            0x01, // address
            0x05, // size
            0x16, // command (0x16 - reply for relay control)
            0x01, // relay No
            0x02, // relay OFF
            0xaf  // sum
        },
        __func__);
}

void TS2KDeviceExpectations::EnqueueReadConfig1()
{
    Expector()->Expect(
        {
            0x01, // address
            0x06, // size
            0x00, // key
            0x05, // command (0x05 - read config)
            0x01, // config No
            0x00, // unused
            0x5a  // sum
        },
        {
            0x01, // address
            0x05, // size
            0x06, // command (0x06 - reply for read config)
            0x01, // config No
            0x02, // config val
            0xe5  // sum
        },
        __func__);
}

void TS2KDeviceExpectations::EnqueueReadConfig2()
{
    Expector()->Expect(
        {
            0x01, // address
            0x06, // size
            0x00, // key
            0x05, // command (0x05 - read config)
            0x02, // config No
            0x00, // unused
            0x0f  // sum
        },
        {
            0x01, // address
            0x05, // size
            0x06, // command (0x06 - reply for read config)
            0x02, // config No
            0x02, // config val
            0xb0  // sum
        },
        __func__);
}
void TS2KDeviceExpectations::EnqueueReadConfig3()
{
    Expector()->Expect(
        {
            0x01, // address
            0x06, // size
            0x00, // key
            0x05, // command (0x05 - read config)
            0x03, // config No
            0x00, // unused
            0xcb  // sum
        },
        {
            0x01, // address
            0x05, // size
            0x06, // command (0x06 - reply for read config)
            0x03, // config No
            0x02, // config val
            0x74  // sum
        },
        __func__);
}
void TS2KDeviceExpectations::EnqueueReadConfig4()
{
    Expector()->Expect(
        {
            0x01, // address
            0x06, // size
            0x00, // key
            0x05, // command (0x05 - read config)
            0x04, // config No
            0x00, // unused
            0xa5  // sum
        },
        {
            0x01, // address
            0x05, // size
            0x06, // command (0x06 - reply for read config)
            0x04, // config No
            0x02, // config val
            0x1a  // sum
        },
        __func__);
}

void TS2KDeviceExpectations::EnqueueReadConfig5()
{
    Expector()->Expect(
        {
            0x01, // address
            0x06, // size
            0x00, // key
            0x05, // command (0x05 - read config)
            0x05, // config No
            0x00, // unused
            0x61  // sum
        },
        {
            0x01, // address
            0x05, // size
            0x06, // command (0x06 - reply for read config)
            0x05, // config No
            0x3c, // config val
            0x7f  // sum
        },
        __func__);
}

void TS2KDeviceExpectations::EnqueueReadConfig6()
{
    Expector()->Expect(
        {
            0x01, // address
            0x06, // size
            0x00, // key
            0x05, // command (0x05 - read config)
            0x06, // config No
            0x00, // unused
            0x34  // sum
        },
        {
            0x01, // address
            0x05, // size
            0x06, // command (0x06 - reply for read config)
            0x06, // config No
            0x05, // config val
            0x08  // sum
        },
        __func__);
}

void TS2KDeviceExpectations::EnqueueReadConfig7()
{
    Expector()->Expect(
        {
            0x01, // address
            0x06, // size
            0x00, // key
            0x05, // command (0x05 - read config)
            0x07, // config No
            0x00, // unused
            0xf0  // sum
        },
        {
            0x01, // address
            0x05, // size
            0x06, // command (0x06 - reply for read config)
            0x07, // config No
            0x3c, // config val
            0xee  // sum
        },
        __func__);
}

void TS2KDeviceExpectations::EnqueueReadConfig8()
{
    Expector()->Expect(
        {
            0x01, // address
            0x06, // size
            0x00, // key
            0x05, // command (0x05 - read config)
            0x08, // config No
            0x00, // unused
            0xfc  // sum
        },
        {
            0x01, // address
            0x05, // size
            0x06, // command (0x06 - reply for read config)
            0x08, // config No
            0x3c, // config val
            0xf6  // sum
        },
        __func__);
}
