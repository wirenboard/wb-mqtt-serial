#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <stdexcept>
#include <algorithm>

#include "pty_based_fake_serial.h"

TPtyBasedFakeSerial::TPtyBasedFakeSerial(TLoggedFixture& fixture):
    Fixture(fixture), Stop(false)
{
    InitPty();
    PtyMasterThread = std::thread(&TPtyBasedFakeSerial::Run, std::ref(*this));
}

TPtyBasedFakeSerial::~TPtyBasedFakeSerial()
{
    {
        std::unique_lock<std::mutex> lk(Mutex);
        Stop = true;
    }
    close(MasterFd);
    Cond.notify_one();
    PtyMasterThread.join();
}

void TPtyBasedFakeSerial::InitPty()
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
            int n = read(MasterFd, &*it, 1);
            if (n < 0)
                throw std::runtime_error("read() failed");
            else if (!n) {
                Fixture.Emit() << "[ERROR] premature eof, pty-based fake serial terminating";
                return;
            }
        }

        auto p = std::mismatch(buf.begin(), buf.end(), exp.ExpectedRequest.begin());
        if (p.first != buf.end())
            Fixture.Emit() << "*> " << buf;

        Fixture.Emit() << "<< " << exp.ResponseToSend;
        for (auto it = exp.ResponseToSend.begin(); it != exp.ResponseToSend.end(); ++it) {
            if (write(MasterFd, &*it, 1) < 1)
                throw std::runtime_error("write() failed");
        }
        Expectations.pop_front();
    }
    Fixture.Emit() << "(pty-based fake serial terminating)";
    if (!Expectations.empty())
        throw std::runtime_error("still got pending expectations");
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

std::string TPtyBasedFakeSerial::GetPtsName() const
{
    return PtsName;
}
