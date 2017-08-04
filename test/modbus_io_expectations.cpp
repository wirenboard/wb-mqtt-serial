#include "modbus_io_expectations.h"


// setup section
void TModbusIOExpectations::EnqueueSetupSectionWriteResponse()
{
    //----- module 1------
    Expector()->Expect(
    WrapPDU({
        0x06,   //function code
        0x2A,   //starting address Hi   (11000)
        0xF8,   //starting address Lo
        0xFF,   //value Hi
        0xFF,   //value Lo
    }),
    WrapPDU({
        0x06,   //function code
        0x2A,   //starting address Hi   (11000)
        0xF8,   //starting address Lo
        0xFF,   //value Hi
        0xFF,   //value Lo
    }), __func__);

    // config flag
    Expector()->Expect(
    WrapPDU({
        0x06,   //function code
        0x27,   //starting address Hi   (10000)
        0x10,   //starting address Lo
        0x00,   //value Hi
        0x01,   //value Lo
    }),
    WrapPDU({
        0x06,   //function code
        0x27,   //starting address Hi   (10000)
        0x10,   //starting address Lo
        0x00,   //value Hi
        0x01,   //value Lo
    }), __func__);
    //----- module 1------

    //----- module 2------
    Expector()->Expect(
    WrapPDU({
        0x06,   //function code
        0x30,   //starting address Hi   (12500)
        0xD4,   //starting address Lo
        0xFF,   //value Hi
        0xFF,   //value Lo
    }),
    WrapPDU({
        0x06,   //function code
        0x30,   //starting address Hi   (12500)
        0xD4,   //starting address Lo
        0xFF,   //value Hi
        0xFF,   //value Lo
    }), __func__);

    // config flag
    Expector()->Expect(
    WrapPDU({
        0x06,   //function code
        0x29,   //starting address Hi   (10501)
        0x05,   //starting address Lo
        0x00,   //value Hi
        0x01,   //value Lo
    }),
    WrapPDU({
        0x06,   //function code
        0x29,   //starting address Hi   (10501)
        0x05,   //starting address Lo
        0x00,   //value Hi
        0x01,   //value Lo
    }), __func__);
    //----- module 2------
}

// read 3 coils
void TModbusIOExpectations::EnqueueCoilReadResponse()
{
    //--------module 1--------
    Expector()->Expect(
    WrapPDU({
        0x01,   //function code
        0x03,   //starting address Hi   (1000)
        0xE8,   //starting address Lo
        0x00,   //quantity Hi
        0x03,   //quantity Lo
    }),
    WrapPDU({
        0x01,   //function code
        0x01,   //byte count
        0x01,   //coils status
    }), __func__);
    //--------module 1--------

    //--------module 2--------
    Expector()->Expect(
    WrapPDU({
        0x01,   //function code
        0x09,   //starting address Hi   (2500)
        0xC4,   //starting address Lo
        0x00,   //quantity Hi
        0x03,   //quantity Lo
    }),
    WrapPDU({
        0x01,   //function code
        0x01,   //byte count
        0x01,   //coils status
    }), __func__);
    //--------module 2--------
}

// write 1 coil on 2 modules
void TModbusIOExpectations::EnqueueCoilWriteResponse()
{
    //------module 1-----
    Expector()->Expect(
    WrapPDU({
        0x05,   //function code
        0x03,   //starting address Hi   (1000)
        0xE8,   //starting address Lo
        0xFF,   //value Hi
        0x00,   //value Lo
    }),
    WrapPDU({
        0x05,   //function code
        0x03,   //starting address Hi   (1000)
        0xE8,   //starting address Lo
        0xFF,   //value Hi
        0x00,   //value Lo
    }), __func__);
    //------module 1-----

    //------module 2-----
    Expector()->Expect(
    WrapPDU({
        0x05,   //function code
        0x09,   //starting address Hi   (2500)
        0xC4,   //starting address Lo
        0x00,   //value Hi
        0x00,   //value Lo
    }),
    WrapPDU({
        0x05,   //function code
        0x09,   //starting address Hi   (2500)
        0xC4,   //starting address Lo
        0x00,   //value Hi
        0x00,   //value Lo
    }), __func__);
    //------module 2-----
}


