#include <cassert>
#include "fake_serial_port.h"

TFakeSerialPort::TFakeSerialPort(TLoggedFixture& fixture)
    : Fixture(fixture), IsPortOpen(false), ResponsePosition(0), ResponseDumpPosition(0) {}

void TFakeSerialPort::CheckPortOpen()
{
    if (!IsPortOpen)
        throw TSerialProtocolException("port not open");
}

void TFakeSerialPort::Open()
{
    if (IsPortOpen)
        throw TSerialProtocolException("port already open");
    Fixture.Emit() << "Open()";
    IsPortOpen = true;
}

void TFakeSerialPort::Close()
{
    CheckPortOpen();
    DumpWhatWasRead();
    Fixture.Emit() << "Close()";
    IsPortOpen = false;
    if (ResponsePosition < ResponseBytes.size())
        throw TSerialProtocolException("not all bytes in the response consumed");
}

bool TFakeSerialPort::IsOpen() const
{
    return IsPortOpen;
}

void TFakeSerialPort::WriteBytes(const uint8_t* buf, int count) {
    DumpWhatWasRead();
    Fixture.Emit() << ">> " << std::vector<uint8_t>(buf, buf + count);
}

uint8_t TFakeSerialPort::ReadByte()
{
    CheckPortOpen();
    std::cerr << "ReadByte(): ResponsePosition: " << ResponsePosition << "; size=" <<
        ResponseBytes.size() << std::endl;
    if (ResponsePosition >= ResponseBytes.size())
        throw TSerialProtocolException("response buffer underflow");
    return ResponseBytes[ResponsePosition++];
}

void TFakeSerialPort::SkipNoise()
{
    CheckPortOpen();
    DumpWhatWasRead();
    Fixture.Emit() << "SkipNoise()";
}

void TFakeSerialPort::DumpWhatWasRead()
{
    assert(ResponseDumpPosition <= ResponsePosition);
    if (ResponseDumpPosition == ResponsePosition)
        return;

    std::vector<uint8_t> slice(ResponseBytes.begin() + ResponseDumpPosition,
                               ResponseBytes.begin() + ResponsePosition);
    Fixture.Emit() << "<< " << slice;
    ResponseDumpPosition = ResponsePosition;
}

void TFakeSerialPort::EnqueueResponse(std::vector<uint8_t> response)
{
    ResponseBytes.insert(ResponseBytes.end(), response.begin(), response.end());
}
