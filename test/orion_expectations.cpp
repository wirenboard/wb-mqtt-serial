#include "orion_expectations.h"


void TOrionDeviceExpectations::EnqueueSetRelayOnResponse()
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
        }, __func__);
}

void TOrionDeviceExpectations::EnqueueSetRelay2On()
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
        }, __func__);
}

void TOrionDeviceExpectations::EnqueueSetRelay3On2()
{
    Expector()->Expect(
        {
            0x01, // address
            0x06, // size
            0x00, // key
            0x15, // command (0x15 - relay control)
            0x03, // relay No
            0x02, // relay OFF
            0x3d  // sum
        },
        {
            0x01, // address
            0x05, // size
            0x16, // command (0x16 - reply for relay control)
            0x03, // relay No
            0x02, // relay OFF
            0x3e  // sum
        }, __func__);
}

void TOrionDeviceExpectations::EnqueueSetRelayOffResponse()
{
    Expector()->Expect(
        {
            0x01, // address
            0x06, // size
            0x00, // key
            0x15, // command (0x15 - relay control)
            0x01, // relay No
            0x00, // relay default
            0x10  // sum
        },
        {
            0x01, // address
            0x05, // size
            0x16, // command (0x16 - reply for relay control)
            0x01, // relay No
            0x00, // relay default
            0x13  // sum
        }, __func__);
}

