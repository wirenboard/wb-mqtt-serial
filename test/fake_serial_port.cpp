#include <cassert>
#include <algorithm>
#include "fake_serial_port.h"

TFakeSerialPort::TFakeSerialPort(TLoggedFixture& fixture)
    : Fixture(fixture), IsPortOpen(false), ReqPos(0), RespPos(0), DumpPos(0) {}

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
    if (ReqPos < Req.size())
        throw TSerialProtocolException("not all of expected requests received");
    if (RespPos < Resp.size())
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

    if (Req.size() - ReqPos < size_t(count) + 1 || Req[ReqPos + count] != FRAME_BOUNDARY)
        throw TSerialProtocolException("Request mismatch");

    const uint8_t* p = buf;
    for (auto it = Req.begin() + ReqPos; p < buf + count; ++p, ++it) {
        if (*it != int(*p))
            throw TSerialProtocolException("Request mismatch");
    }

    if (Req[ReqPos + count] != FRAME_BOUNDARY)
        throw TSerialProtocolException("Unexpectedly short request");

    ReqPos += count + 1;
}

uint8_t TFakeSerialPort::ReadByte()
{
    CheckPortOpen();

    while (RespPos < Resp.size() && Resp[RespPos] == FRAME_BOUNDARY)
        RespPos++;

    if (RespPos == Resp.size())
        throw TSerialProtocolException("response buffer underflow");

    return Resp[RespPos++];
}

int TFakeSerialPort::ReadFrame(uint8_t* buf, int count, int)
{
    int nread = 0;
    for (; nread < count; ++nread) {
        if (RespPos == Resp.size())
            break;
        int b = Resp[RespPos++];
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
    assert(DumpPos <= RespPos);
    if (DumpPos == RespPos)
        return;

    std::vector<uint8_t> slice;
    for (; DumpPos < RespPos; ++DumpPos) {
        if (Resp[DumpPos] == FRAME_BOUNDARY) {
            if (slice.size() > 0)
                Fixture.Emit() << "<< " << slice;
            slice.clear();
        } else
            slice.push_back(Resp[DumpPos]);
    }

    if (slice.size() > 0)
        Fixture.Emit() << "<< " << slice;

    DumpPos = RespPos;
}

void TFakeSerialPort::Expect(const std::vector<int>& request, const std::vector<int>& response)
{
    Req.insert(Req.end(), request.begin(), request.end());
    Req.push_back(FRAME_BOUNDARY);
    Resp.insert(Resp.end(), response.begin(), response.end());
    Resp.push_back(FRAME_BOUNDARY);
}

void TFakeSerialPort::SkipFrameBoundary()
{
    if (RespPos < Resp.size() && Resp[RespPos] == FRAME_BOUNDARY)
        RespPos++;
}
