#include "fake_serial_port.h"
#include "memory_block.h"
#include "ir_device_query_factory.h"
#include "utils.h"
#include "binary_semaphore.h"

#include <cassert>
#include <algorithm>
#include <stdexcept>

using namespace std;

TFakeSerialPort::TFakeSerialPort(TLoggedFixture& fixture)
    : Fixture(fixture), IsPortOpen(false), DoSimulateDisconnect(false), ReqPos(0), RespPos(0), DumpPos(0) {}

void TFakeSerialPort::SetDebug(bool debug)
{
    Fixture.Emit() << "SetDebug(" << debug << ")";
}

bool TFakeSerialPort::Debug() const
{
    return false;
}

void TFakeSerialPort::SetExpectedFrameTimeout(const chrono::microseconds& timeout)
{
    ExpectedFrameTimeout = timeout;
}

void TFakeSerialPort::CheckPortOpen() const
{
    if (!IsPortOpen)
        throw TSerialDeviceException("port not open");
}

void TFakeSerialPort::Open()
{
    if (IsPortOpen)
        throw TSerialDeviceException("port already open");
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
        throw runtime_error("not all of expected requests received");
    if (RespPos < Resp.size())
        throw runtime_error("not all bytes in the response consumed");
}

TLoggedFixture & TFakeSerialPort::GetFixture()
{
    return Fixture;
}

bool TFakeSerialPort::IsOpen() const
{
    return IsPortOpen;
}

void TFakeSerialPort::WriteBytes(const uint8_t* buf, int count) {
    if (DoSimulateDisconnect) {
        return;
    }

    SkipFrameBoundary();
    DumpWhatWasRead();
    if (!PendingFuncs.empty()) {
        Fixture.Emit() << PendingFuncs.front() << "()";
        PendingFuncs.pop_front();
    }
    Fixture.Emit() << ">> " << vector<uint8_t>(buf, buf + count);
    auto start = Req.begin() + ReqPos;
    try {
        if (Req.size() - ReqPos < size_t(count) + 1 || Req[ReqPos + count] != FRAME_BOUNDARY)
            throw runtime_error("Request mismatch");

        const uint8_t* p = buf;
        for (auto it = start; p < buf + count; ++p, ++it) {
            if (*it != int(*p))
                throw runtime_error(string("Request mismatch: ") +
                        HexDump(vector<uint8_t>(buf, buf + count)) +
                        ", expected: " +
                        HexDump(vector<uint8_t>(start, start + count))
                    );
        }


        if (Req[ReqPos + count] != FRAME_BOUNDARY)
            throw runtime_error("Unexpectedly short request");

        ReqPos += count + 1;
    } catch (const runtime_error& e) {
        auto stop = find(start, Req.end(), FRAME_BOUNDARY);
        if (start != stop)
            Fixture.Emit() << "*> " << vector<uint8_t>(start, stop);
        else
            Fixture.Emit() << "*> req empty";
        throw;
    }
}

uint8_t TFakeSerialPort::ReadByte()
{
    if (DoSimulateDisconnect) {
        return 0xff;
    }
    CheckPortOpen();

    while (RespPos < Resp.size() && Resp[RespPos] == FRAME_BOUNDARY)
        RespPos++;

    if (RespPos == Resp.size())
        throw runtime_error("response buffer underflow");

    return Resp[RespPos++];
}

int TFakeSerialPort::ReadFrame(uint8_t* buf, int count,
                               const chrono::microseconds& timeout,
                               TFrameCompletePred frame_complete)
{
    if (DoSimulateDisconnect) {
        return 0;
    }
    if (ExpectedFrameTimeout.count() >= 0 && timeout != ExpectedFrameTimeout)
        throw runtime_error("TFakeSerialPort::ReadFrame: bad timeout: " +
                                 to_string(timeout.count()) + " instead of " +
                                 to_string(ExpectedFrameTimeout.count()));
    int nread = 0;
    uint8_t* p = buf;
    for (; nread < count; ++nread) {
        if (RespPos == Resp.size())
            break;
        int b = Resp[RespPos++];
        // TBD: after frame_ready arg is added,
        // make sure the frame becomes 'ready' exactly on the boundary.
        // Note though that frame_ready will be optional.
        if (b == FRAME_BOUNDARY)
            break;
        *p++ = (uint8_t)b;
    }
    if (frame_complete && !frame_complete(buf, nread))
        throw runtime_error("incomplete frame read");
    return nread;
}

void TFakeSerialPort::SkipNoise()
{
    CheckPortOpen();
    SkipFrameBoundary();
    DumpWhatWasRead();
    Fixture.Emit() << "SkipNoise()";
}

void TFakeSerialPort::Sleep(const chrono::microseconds& us)
{
    SkipFrameBoundary();
    DumpWhatWasRead();
    Fixture.Emit() << "Sleep(" << us.count() << ")";
}

bool TFakeSerialPort::Wait(const PBinarySemaphore & semaphore, const TTimePoint & until)
{
    if (semaphore->TryWait())
        return true;
    if (until < Time)
        throw runtime_error("TFakeSerialPort::Wait(): going back in time");
    Time = until;
    return false;
}

TTimePoint TFakeSerialPort::CurrentTime() const
{
    return Time;
}

void TFakeSerialPort::CycleEnd(bool ok)
{
    Fixture.Emit() << (ok ? "Port cycle OK" : "Port cycle FAIL");
}

void TFakeSerialPort::DumpWhatWasRead()
{
    assert(DumpPos <= RespPos);
    if (DumpPos == RespPos)
        return;

    vector<uint8_t> slice;
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

void TFakeSerialPort::Elapse(const chrono::milliseconds& ms)
{
    Time += ms;
}

void TFakeSerialPort::SimulateDisconnect(bool simulate)
{
    DoSimulateDisconnect = simulate;
}

bool TFakeSerialPort::GetDoSimulateDisconnect() const
{
    return DoSimulateDisconnect;
}

void TFakeSerialPort::Expect(const vector<int>& request, const vector<int>& response, const char* func)
{
    if (func)
        PendingFuncs.push_back(func);
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

void TSerialDeviceTest::SetUp()
{
    SerialPort = PFakeSerialPort(new TFakeSerialPort(*this));
}

PExpector TSerialDeviceTest::Expector() const
{
    return SerialPort;
}

PIRDeviceQuery TSerialDeviceTest::GetReadQuery(vector<PVirtualValue> && vRegs) const
{
    TPSet<PMemoryBlock> mbs;

    for (const auto & val: vRegs) {
        const auto & vreg = dynamic_pointer_cast<TVirtualRegister>(val);
        const auto & regMbs = vreg->GetMemoryBlocks();
        mbs.insert(regMbs.begin(), regMbs.end());
    }

    return TIRDeviceQueryFactory::CreateQuery<TIRDeviceQuery>({
        move(mbs),
        move(vRegs)
    });
}

void TSerialDeviceTest::TestRead(const PIRDeviceQuery & query) const
{
    query->GetDevice()->Execute(query);
    assert(EQueryStatus::Ok == query->GetStatus());
    query->ResetStatus();
    query->InvalidateReadValues();
}

void TSerialDeviceTest::TearDown()
{
    SerialPort.reset();
    TLoggedFixture::TearDown();
}

void TSerialDeviceIntegrationTest::SetUp()
{
    TSerialDeviceTest::SetUp();
    PortMakerCalled = false;

    PTemplateMap templateMap = make_shared<TTemplateMap>();
    if (GetTemplatePath()) {
        TConfigTemplateParser templateParser(GetDataFilePath(GetTemplatePath()), false);
        templateMap = templateParser.Parse();
    }
    TConfigParser parser(GetDataFilePath(ConfigPath()), false, templateMap);


    Config = parser.Parse();
    MQTTClient = PFakeMQTTClient(new TFakeMQTTClient("em-test", *this));
    Observer = PMQTTSerialObserver(new TMQTTSerialObserver(MQTTClient, Config, SerialPort));
}

void TSerialDeviceIntegrationTest::TearDown()
{
    MQTTClient->Unobserve(Observer);
    Observer.reset();
    TSerialDeviceTest::TearDown();
}
