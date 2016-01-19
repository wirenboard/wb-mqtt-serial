#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <stdexcept>
#include <algorithm>

#include "pty_based_fake_serial.h"

namespace {
    const int SELECT_PERIOD_MS = 50;
}

void TPtyBasedFakeSerial::PtyPair::Init()
{
    MasterFd = posix_openpt(O_RDWR | O_NOCTTY);
    if (MasterFd < 0)
        throw std::runtime_error("posix_openpt() failed");

    if (grantpt(MasterFd) < 0) {
        close(MasterFd);
        throw std::runtime_error("grantpt() failed");
    }

    if (unlockpt(MasterFd) < 0) {
        close(MasterFd);
        throw std::runtime_error("unlockpt() failed");
    }

    char* ptsName = ptsname(MasterFd);
    if (!ptsName) {
        close(MasterFd);
        throw std::runtime_error("ptsname() failed");
    }

    PtsName = ptsName;
}

TPtyBasedFakeSerial::TPtyBasedFakeSerial(TLoggedFixture& fixture):
    Fixture(fixture), Stop(false), ForwardingFromPrimary(false)
{
    Primary.Init();
}

TPtyBasedFakeSerial::~TPtyBasedFakeSerial()
{
    {
        std::unique_lock<std::mutex> lk(Mutex);
        Stop = true;
    }
    close(Primary.MasterFd);
    Cond.notify_one();
    PtyMasterThread.join();
}

void TPtyBasedFakeSerial::StartExpecting()
{
    PtyMasterThread = std::thread(&TPtyBasedFakeSerial::Run, std::ref(*this));
}

void TPtyBasedFakeSerial::StartForwarding()
{
    Secondary.Init();
    PtyMasterThread = std::thread(&TPtyBasedFakeSerial::Forward, std::ref(*this));
}

void TPtyBasedFakeSerial::Run()
{
    for (;;) {
        std::unique_lock<std::mutex> lk(Mutex);
        Cond.wait(lk, [this]{ return Stop || !Expectations.empty(); });

        if (Stop)
            break;

        const Expectation& exp = Expectations.front();
        if (exp.Func)
            Fixture.Emit() << exp.Func << "()";
        Fixture.Emit() << ">> " << exp.ExpectedRequest;

        std::vector<uint8_t> buf(exp.ExpectedRequest.size());
        for (auto it = buf.begin(); it != buf.end(); ++it) {
            int n = read(Primary.MasterFd, &*it, 1);
            if (!n || (n < 0 && errno == EIO)) {
                Fixture.Emit() << "[ERROR] premature eof, pty-based fake serial terminating";
                return;
            } else if (n < 0)
                throw std::runtime_error("read() failed");
        }

        auto p = std::mismatch(buf.begin(), buf.end(), exp.ExpectedRequest.begin());
        if (p.first != buf.end())
            Fixture.Emit() << "*> " << buf;

        Fixture.Emit() << "<< " << exp.ResponseToSend;
        for (auto it = exp.ResponseToSend.begin(); it != exp.ResponseToSend.end(); ++it) {
            if (write(Primary.MasterFd, &*it, 1) < 1)
                throw std::runtime_error("write() failed");
        }
        Expectations.pop_front();
    }
    Fixture.Emit() << "(pty-based fake serial terminating)";
    if (!Expectations.empty())
        throw std::runtime_error("still got pending expectations");
}

void TPtyBasedFakeSerial::Forward()
{
    for (;;) {
        {
            std::unique_lock<std::mutex> lk(Mutex);
            if (Stop)
                break;
        }
        fd_set rfds;
        struct timeval tv, *tvp = 0;

        FD_ZERO(&rfds);
        FD_SET(Primary.MasterFd, &rfds);
        FD_SET(Secondary.MasterFd, &rfds);

        tv.tv_sec = SELECT_PERIOD_MS / 1000;
        tv.tv_usec = (SELECT_PERIOD_MS % 1000) * 1000;
        tvp = &tv;

        int r = select(std::max(Primary.MasterFd, Secondary.MasterFd) + 1, &rfds, NULL, NULL, tvp);
        if (r < 0)
            throw std::runtime_error("select() failed");

        if (!r)
            continue;

        int read_from, write_to;
        if (FD_ISSET(Primary.MasterFd, &rfds)) {
            if (!ForwardingFromPrimary)
                FlushForwardingLogs();
            ForwardingFromPrimary = true;
            read_from = Primary.MasterFd;
            write_to = Secondary.MasterFd;
        } else if (FD_ISSET(Secondary.MasterFd, &rfds)) {
            if (ForwardingFromPrimary)
                FlushForwardingLogs();
            ForwardingFromPrimary = false;
            read_from = Secondary.MasterFd;
            write_to = Primary.MasterFd;
        }

        uint8_t b;
        int n = read(read_from, &b, 1);
        if (n < 0) {
            if (errno == EIO) // terminal closed
                break;
            throw std::runtime_error("forwarding: read() failed");
        }
        if (n == 0)
            break;
        n = write(write_to, &b, 1);
        if (n < 1)
            throw std::runtime_error("forwarding: write() failed");
        ForwardedBytes.push_back(b);
    }
    FlushForwardingLogs();
    Fixture.Emit() << "(pty-based fake serial -- stopping forwarding)";
}

void TPtyBasedFakeSerial::FlushForwardingLogs()
{
    if (ForwardedBytes.empty())
        return;
    Fixture.Emit() << (ForwardingFromPrimary ? ">> " : "<< ") << ForwardedBytes;
    ForwardedBytes.clear();
}

void TPtyBasedFakeSerial::Expect(const std::vector<int>& request, const std::vector<int>& response, const char* func)
{
    {
        std::unique_lock<std::mutex> lk(Mutex);
        Expectations.emplace_back(std::vector<uint8_t>(request.begin(), request.end()),
                                  std::vector<uint8_t>(response.begin(), response.end()),
                                  func);
    }
    Cond.notify_one();
}

std::string TPtyBasedFakeSerial::GetPrimaryPtsName() const
{
    return Primary.PtsName;
}

std::string TPtyBasedFakeSerial::GetSecondaryPtsName() const
{
    return Secondary.PtsName;
}
