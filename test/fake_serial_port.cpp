#include <cassert>
#include "fake_serial_port.h"

TFakeSerialPort::TFakeSerialPort(TLoggedFixture& fixture)
    : Fixture(fixture), IsPortOpen(false), Pos(0), DumpPos(0) {}

void TFakeSerialPort::SetDebug(bool debug)
{
    Fixture.Emit() << "SetDebug(" << debug << ")";
}

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
    SkipFrameBoundary();
    DumpWhatWasRead();
    Fixture.Emit() << "Close()";
    IsPortOpen = false;
    if (Pos < Resp.size())
        throw TSerialProtocolException("not all bytes in the response consumed");
}

bool TFakeSerialPort::IsOpen() const
{
    return IsPortOpen;
}

void TFakeSerialPort::WriteBytes(const uint8_t* buf, int count) {
    SkipFrameBoundary();
    DumpWhatWasRead();
    Fixture.Emit() << ">> " << std::vector<uint8_t>(buf, buf + count);
}

uint8_t TFakeSerialPort::ReadByte()
{
    CheckPortOpen();

    while (Pos < Resp.size() && Resp[Pos] == FRAME_BOUNDARY)
        Pos++;

    if (Pos == Resp.size())
        throw TSerialProtocolException("response buffer underflow");

    return Resp[Pos++];
}

int TFakeSerialPort::ReadFrame(uint8_t* buf, int count)
{
    int nread = 0;
    for (; nread < count; ++nread) {
        if (Pos == Resp.size())
            break;
        int b = Resp[Pos++];
        if (b == FRAME_BOUNDARY)
            break;
        *buf++ = (uint8_t)b;
    }
    return nread;
}

void TFakeSerialPort::SkipNoise()
{
    CheckPortOpen();
    SkipFrameBoundary();
    DumpWhatWasRead();
    Fixture.Emit() << "SkipNoise()";
}

void TFakeSerialPort::USleep(int usec)
{
    SkipFrameBoundary();
    DumpWhatWasRead();
    Fixture.Emit() << "Sleep(" << usec << ")";
}

void TFakeSerialPort::DumpWhatWasRead()
{
    assert(DumpPos <= Pos);
    if (DumpPos == Pos)
        return;

    std::vector<uint8_t> slice;
    for (; DumpPos < Pos; ++DumpPos) {
        if (Resp[DumpPos] == FRAME_BOUNDARY) {
            if (slice.size() > 0)
                Fixture.Emit() << "<< " << slice;
            slice.clear();
        } else
            slice.push_back(Resp[DumpPos]);
    }

    if (slice.size() > 0)
        Fixture.Emit() << "<< " << slice;

    DumpPos = Pos;
}

void TFakeSerialPort::EnqueueResponse(const std::vector<int>& frame)
{
    Resp.insert(Resp.end(), frame.begin(), frame.end());
    Resp.push_back(FRAME_BOUNDARY);
}

void TFakeSerialPort::SkipFrameBoundary()
{
    if (Pos < Resp.size() && Resp[Pos] == FRAME_BOUNDARY)
        Pos++;
}
