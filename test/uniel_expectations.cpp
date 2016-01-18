#include "uniel_expectations.h"

void TUnielProtocolExpectations::EnqueueVoltageQueryResponse()
{
    Expector()->Expect(
        {
            0xff, // sync
            0xff, // sync
            0x05, // op (5 = read)
            0x01, // unit id
            0x00, // not used
            0x0a, // addr
            0x00, // not used
            0x10  // sum
        },
        {
            0xff, // sync
            0xff, // sync
            0x05, // op (5 = read)
            0x00, // unit id 0 = module response
            0x9a, // value = 9a (154)
            0x0a, // addr
            0x00, // not used
            0xa9  // sum
        }, __func__);
}

void TUnielProtocolExpectations::EnqueueRelayOffQueryResponse()
{
    Expector()->Expect(
        {
            0xff, // sync
            0xff, // sync
            0x05, // op (5 = read)
            0x01, // unit id
            0x00, // not used
            0x1b, // addr
            0x00, // not used
            0x21  // sum
        },
        {
            0xff, // sync
            0xff, // sync
            0x05, // op (5 = read)
            0x00, // unit id 0 = module response
            0x00, // value = 00 (off)
            0x1b, // addr
            0x00, // not used
            0x20  // sum
        }, __func__);
}

void TUnielProtocolExpectations::EnqueueRelayOnQueryResponse()
{
    Expector()->Expect(
        {
            0xff, // sync
            0xff, // sync
            0x05, // op (5 = read)
            0x01, // unit id
            0x00, // not used
            0x1b, // addr
            0x00, // not used
            0x21  // sum
        },
        {
            0xff, // sync
            0xff, // sync
            0x05, // op (5 = read)
            0x00, // unit id 0 = module response
            0xff, // value = 0xff (on)
            0x1b, // addr
            0x00, // not used
            0x1f  // sum
        }, __func__);
}

void TUnielProtocolExpectations::EnqueueThreshold0QueryResponse()
{
    Expector()->Expect(
        {
            0xff, // sync
            0xff, // sync
            0x05, // op (5 = read)
            0x01, // unit id
            0x00, // not used
            0x02, // addr
            0x00, // not used
            0x08  // sum
        },
        {
            0xff, // sync
            0xff, // sync
            0x05, // op (5 = read)
            0x00, // unit id 0 = module response
            0x70, // value = 0xff (on)
            0x02, // addr
            0x00, // not used
            0x77  // sum
        }, __func__);
}

void TUnielProtocolExpectations::EnqueueBrightnessQueryResponse()
{
    Expector()->Expect(
        {
            0xff, // sync
            0xff, // sync
            0x05, // op (5 = read)
            0x01, // unit id
            0x00, // not used
            0x41, // addr
            0x00, // not used
            0x47  // sum
        },
        {
            0xff, // sync
            0xff, // sync
            0x05, // op (5 = read)
            0x00, // unit id 0 = module response
            0x42, // value = 66
            0x41, // addr
            0x00, // not used
            0x88  // sum
        }, __func__);
}

void TUnielProtocolExpectations::EnqueueSetRelayOnResponse()
{
    Expector()->Expect(
        {
            0xff, // sync
            0xff, // sync
            0x06, // op (6 = write)
            0x01, // unit id
            0xff, // relay on
            0x1b, // addr
            0x00, // not used
            0x21  // sum
        },
        {
            0xff, // sync
            0xff, // sync
            0x06, // op (6 = write)
            0x00, // unit id 0 = module response
            0xff, // relay on
            0x1b, // addr
            0x00, // not used
            0x20  // sum
        }, __func__);
}

void TUnielProtocolExpectations::EnqueueSetRelayOffResponse()
{
    Expector()->Expect(
        {
            0xff, // sync
            0xff, // sync
            0x06, // op (6 = write)
            0x01, // unit id
            0x00, // relay off
            0x1b, // addr
            0x00, // not used
            0x22  // sum
        },
        {
            0xff, // sync
            0xff, // sync
            0x06, // op (6 = write)
            0x00, // unit id 0 = module response
            0x00, // relay on
            0x1b, // addr
            0x00, // not used
            0x21  // sum
        }, __func__);
}

void TUnielProtocolExpectations::EnqueueSetLowThreshold0Response()
{
    Expector()->Expect(
        {
            0xff, // sync
            0xff, // sync
            0x06, // op (6 = write)
            0x01, // unit id
            0x70, // value = 112
            0x02, // addr
            0x00, // not used
            0x79  // sum
        },
        {
            0xff, // sync
            0xff, // sync
            0x06, // op (6 = write)
            0x00, // unit id 0 = module response
            0x70, // value = 112
            0x02, // addr
            0x00, // not used
            0x78  // sum
        }, __func__);
}

void TUnielProtocolExpectations::EnqueueSetBrightnessResponse()
{
    Expector()->Expect(
        {
            0xff, // sync
            0xff, // sync
            0x0a, // op (0x0a = set brightness)
            0x01, // unit id
            0x42, // value = 66
            0x01, // addr
            0x00, // not used
            0x4e  // sum
        },
        {
            0xff, // sync
            0xff, // sync
            0x0a, // op (0x0a = set brightness)
            0x00, // unit id 0 = module response
            0x42, // value = 66
            0x01, // addr
            0x00, // not used
            0x4d  // sum
        }, __func__);
}
