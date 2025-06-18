#include "modbus_io_expectations.h"

// setup section
void TModbusIOExpectations::EnqueueSetupSectionWriteResponse(bool firstModule, bool error)
{
    Expector()->Expect(WrapPDU({
                           0x06, // function code
                           0x00, // starting address Hi
                           0x72, // starting address Lo (114)
                           0x00, // value Hi
                           0x01, // value Lo
                       }),
                       WrapPDU({
                           0x06, // function code
                           0x00, // starting address Hi   (114)
                           0x72, // starting address Lo
                           0x00, // value Hi
                           0x01, // value Lo
                       }),
                       __func__);

    if (firstModule) {
        Expector()->Expect(WrapPDU({
                               0x06, // function code
                               0x2A, // starting address Hi   (10999)
                               0xF7, // starting address Lo
                               0xFF, // value Hi
                               0xFF, // value Lo
                           }),
                           WrapPDU({
                               0x06, // function code
                               0x2A, // starting address Hi   (10999)
                               0xF7, // starting address Lo
                               0xFF, // value Hi
                               0xFF, // value Lo
                           }),
                           __func__);

        // config flag
        if (error) {
            Expector()->Expect(WrapPDU({
                                   0x06, // function code
                                   0x2A, // starting address Hi   (11000)
                                   0xF8, // starting address Lo
                                   0x00, // value Hi
                                   0x01, // value Lo
                               }),
                               WrapPDU({
                                   0x86, // Exception
                                   0x04, // SLAVE DEVICE FAILURE
                               }),
                               __func__);
        } else {
            Expector()->Expect(WrapPDU({
                                   0x06, // function code
                                   0x2A, // starting address Hi   (11000)
                                   0xF8, // starting address Lo
                                   0x00, // value Hi
                                   0x01, // value Lo
                               }),
                               WrapPDU({
                                   0x06, // function code
                                   0x2A, // starting address Hi   (11000)
                                   0xF8, // starting address Lo
                                   0x00, // value Hi
                                   0x01, // value Lo
                               }),
                               __func__);
        }
        return;
    }
    Expector()->Expect(WrapPDU({
                           0x06, // function code
                           0x30, // starting address Hi   (12499)
                           0xD3, // starting address Lo
                           0xFF, // value Hi
                           0xFF, // value Lo
                       }),
                       WrapPDU({
                           0x06, // function code
                           0x30, // starting address Hi   (12499)
                           0xD3, // starting address Lo
                           0xFF, // value Hi
                           0xFF, // value Lo
                       }),
                       __func__);

    // config flag
    if (error) {
        Expector()->Expect(WrapPDU({
                               0x06, // function code
                               0x30, // starting address Hi   (12500)
                               0xD4, // starting address Lo
                               0x00, // value Hi
                               0x01, // value Lo
                           }),
                           WrapPDU({
                               0x86, // Exception
                               0x02, // ILLEGAL DATA ADDRESS
                           }),
                           __func__);
    } else {
        Expector()->Expect(WrapPDU({
                               0x06, // function code
                               0x30, // starting address Hi   (12500)
                               0xD4, // starting address Lo
                               0x00, // value Hi
                               0x01, // value Lo
                           }),
                           WrapPDU({
                               0x06, // function code
                               0x30, // starting address Hi   (12500)
                               0xD4, // starting address Lo
                               0x00, // value Hi
                               0x01, // value Lo
                           }),
                           __func__);
    }
}

// read 3 coils
void TModbusIOExpectations::EnqueueCoilReadResponse(bool firstModule)
{
    if (firstModule) {
        Expector()->Expect(WrapPDU({
                               0x01, // function code
                               0x03, // starting address Hi   (1000)
                               0xE8, // starting address Lo
                               0x00, // quantity Hi
                               0x03, // quantity Lo
                           }),
                           WrapPDU({
                               0x01, // function code
                               0x01, // byte count
                               0x01, // coils status
                           }),
                           __func__);
        return;
    }
    Expector()->Expect(WrapPDU({
                           0x01, // function code
                           0x09, // starting address Hi   (2500)
                           0xC4, // starting address Lo
                           0x00, // quantity Hi
                           0x03, // quantity Lo
                       }),
                       WrapPDU({
                           0x01, // function code
                           0x01, // byte count
                           0x01, // coils status
                       }),
                       __func__);
}

// write 1 coil on 2 modules
void TModbusIOExpectations::EnqueueCoilWriteResponse(bool firstModule)
{
    if (firstModule) {
        Expector()->Expect(WrapPDU({
                               0x05, // function code
                               0x03, // starting address Hi   (1000)
                               0xE8, // starting address Lo
                               0xFF, // value Hi
                               0x00, // value Lo
                           }),
                           WrapPDU({
                               0x05, // function code
                               0x03, // starting address Hi   (1000)
                               0xE8, // starting address Lo
                               0xFF, // value Hi
                               0x00, // value Lo
                           }),
                           __func__);
    } else {
        Expector()->Expect(WrapPDU({
                               0x05, // function code
                               0x09, // starting address Hi   (2500)
                               0xC4, // starting address Lo
                               0x00, // value Hi
                               0x00, // value Lo
                           }),
                           WrapPDU({
                               0x05, // function code
                               0x09, // starting address Hi   (2500)
                               0xC4, // starting address Lo
                               0x00, // value Hi
                               0x00, // value Lo
                           }),
                           __func__);
    }
}
