#pragma once
#include <thread>
#include <deque>
#include <vector>
#include <string>
#include <mutex>
#include <memory>
#include <condition_variable>
#include "expector.h"
#include "testlog.h"

class TPtyBasedFakeSerial: public TExpector {
public:
    TPtyBasedFakeSerial(TLoggedFixture& fixture);
    ~TPtyBasedFakeSerial();
    void Expect(const std::vector<int>& request, const std::vector<int>& response, const char* func = 0);
    std::string GetPrimaryPtsName() const;
    std::string GetSecondaryPtsName() const;
    void StartExpecting();
    void StartForwarding();
    void Flush();
private:
    struct PtyPair {
        void Init();
        int MasterFd;
        std::string PtsName;
    };
    struct Expectation {
        Expectation(const std::vector<uint8_t> expectedRequest,
                    const std::vector<uint8_t> responseToSend,
                    const char* func):
            ExpectedRequest(expectedRequest),
            ResponseToSend(responseToSend),
            Func(func) {}
        std::vector<uint8_t> ExpectedRequest, ResponseToSend;
        const char* Func;
    };

    void Run();
    void Forward();
    void FlushForwardingLogs();

    TLoggedFixture& Fixture;
    PtyPair Primary, Secondary;
    bool Stop, ForceFlush, ForwardingFromPrimary;
    std::vector<uint8_t> ForwardedBytes;
    std::thread PtyMasterThread;
    std::deque<Expectation> Expectations;
    std::mutex Mutex;
    std::condition_variable Cond, FlushCond;
};

typedef std::shared_ptr<TPtyBasedFakeSerial> PPtyBasedFakeSerial;
