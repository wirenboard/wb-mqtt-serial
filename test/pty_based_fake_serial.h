#pragma once
#include "expector.h"
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <wblib/testing/testlog.h>

class TPtyBasedFakeSerial: public TExpector
{
public:
    TPtyBasedFakeSerial(WBMQTT::Testing::TLoggedFixture& fixture);
    ~TPtyBasedFakeSerial();
    void Expect(const std::vector<int>& request, const std::vector<int>& response, const char* func = 0);
    std::string GetPrimaryPtsName() const;
    std::string GetSecondaryPtsName() const;
    void StartExpecting();
    void StartForwarding();
    void Flush();
    void SetDumpForwardingLogs(bool val)
    {
        DumpForwardingLogs = val;
    };

private:
    struct PtyPair
    {
        ~PtyPair();
        void Init();

        int MasterFd = -1;
        std::string PtsName;
    };
    struct Expectation
    {
        Expectation(const std::vector<uint8_t> expectedRequest,
                    const std::vector<uint8_t> responseToSend,
                    const char* func)
            : ExpectedRequest(expectedRequest),
              ResponseToSend(responseToSend),
              Func(func)
        {}
        std::vector<uint8_t> ExpectedRequest, ResponseToSend;
        const char* Func;
    };

    void Run();
    void Forward();
    void FlushForwardingLogs();

    WBMQTT::Testing::TLoggedFixture& Fixture;
    PtyPair Primary, Secondary;
    bool Stop, ForceFlush, ForwardingFromPrimary;
    std::vector<uint8_t> ForwardedBytes;
    std::thread PtyMasterThread;
    std::deque<Expectation> Expectations;
    std::mutex Mutex;
    std::condition_variable Cond, FlushCond;
    bool DumpForwardingLogs = true;
};

typedef std::shared_ptr<TPtyBasedFakeSerial> PPtyBasedFakeSerial;
