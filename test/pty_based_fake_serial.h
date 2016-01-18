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
    std::string GetPtsName() const;
private:
    void InitPty();
    void Run();
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

    TLoggedFixture& Fixture;
    bool Stop;
    int MasterFd;
    std::thread PtyMasterThread;
    std::vector<uint8_t> input;
    std::deque<Expectation> Expectations;
    std::string PtsName;
    std::mutex Mutex;
    std::condition_variable Cond;
};

typedef std::shared_ptr<TPtyBasedFakeSerial> PPtyBasedFakeSerial;
