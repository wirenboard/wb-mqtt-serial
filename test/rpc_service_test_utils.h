#pragma once

#include <atomic>
#include <condition_variable>
#include <future>
#include <map>
#include <queue>
#include <thread>

#include <wblib/rpc.h>

#include "serial_client.h"
#include "serial_driver_core.h"

//! The tests run under valgrind, the waits for other threads are generous
const std::chrono::seconds SERVICE_TEST_WAIT_TIMEOUT(30);

//! The schemas of the repository and the templates of the directories
TSerialDriverCoreFiles MakeSerialDriverCoreFiles(const std::string& configPath,
                                                 const std::string& templatesDir,
                                                 const std::string& userTemplatesDir);

//! Keeps the methods as the handlers register them, the test calls them the way the RPC server does
class TRPCMethods: public WBMQTT::TMqttRpcServer
{
public:
    ~TRPCMethods();

    void RegisterMethod(const std::string& service, const std::string& method, TMethodHandler handler) override;
    void RegisterAsyncMethod(const std::string& service,
                             const std::string& method,
                             TAsyncMethodHandler handler) override;
    void Start() override;
    void Stop() override;

    Json::Value Call(const std::string& method, const Json::Value& params);

    //! A synchronous method is queued, the queued methods are called one by one in the thread of the server.
    //! Stop drops the methods which are not called yet
    void CallAsync(const std::string& method,
                   const Json::Value& params,
                   TResultCallback onResult,
                   TErrorCallback onError);

private:
    std::map<std::string, TMethodHandler> Methods;
    std::map<std::string, TAsyncMethodHandler> AsyncMethods;

    std::mutex Mutex;
    std::condition_variable JobAdded;
    std::queue<std::function<void()>> Jobs;
    bool Stopped = false;
    std::thread Thread;

    void RunJobs();
};

//! Occupies the port until Release, as a long exchange with a device does
class TBlockingTask: public ISerialClientTask
{
public:
    ISerialClientTask::TRunResult Run(PFeaturePort port,
                                      TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                      const std::list<PSerialDevice>& polledDevices) override;

    bool WaitStarted(std::chrono::milliseconds timeout);
    void Release();

private:
    std::promise<void> Started;
    std::future<void> StartedFuture = Started.get_future();
    std::promise<void> ReleasePromise;
    std::shared_future<void> ReleaseFuture = ReleasePromise.get_future().share();
    std::atomic<bool> Released{false};
};

//! The answer of an asynchronous RPC method
class TAsyncAnswer
{
public:
    void SetResult(const Json::Value& result);
    void SetError(const std::string& error);

    bool IsAnswered() const;
    bool Wait(std::chrono::milliseconds timeout) const;

    //! Waits for the answer: the error, an empty string on success, "no answer" if there is none in time
    std::string Get() const;

    //! Waits for the answer, null on an error
    Json::Value GetResult() const;

private:
    void Set(const Json::Value& result, const std::string& error);

    std::promise<std::pair<Json::Value, std::string>> Answer;
    std::shared_future<std::pair<Json::Value, std::string>> AnswerFuture = Answer.get_future().share();
    std::atomic<bool> Answered{false};
};

typedef std::shared_ptr<TAsyncAnswer> PAsyncAnswer;

PAsyncAnswer CallAsync(TRPCMethods& rpc, const std::string& method, const Json::Value& params);

//! Gives the task made beforehand and keeps the parameters cache it is made with
class TMakeTask
{
public:
    TMakeTask(PSerialClientTask task);

    PSerialClientTask operator()(PSerialDevice, PDeviceParametersCache cache);

    PDeviceParametersCache GetCache() const;

private:
    PSerialClientTask Task;
    PDeviceParametersCache Cache;
};
