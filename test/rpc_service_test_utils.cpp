#include "rpc_service_test_utils.h"

#include <wblib/testing/testlog.h>

TSerialDriverCoreFiles MakeSerialDriverCoreFiles(const std::string& configPath,
                                                 const std::string& templatesDir,
                                                 const std::string& userTemplatesDir)
{
    using WBMQTT::Testing::TLoggedFixture;
    return TSerialDriverCoreFiles{configPath,
                                  templatesDir,
                                  userTemplatesDir,
                                  TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"),
                                  TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-confed-common.schema.json"),
                                  TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-ports.schema.json"),
                                  TLoggedFixture::GetDataFilePath("../protocols")};
}

TRPCMethods::~TRPCMethods()
{
    Stop();
}

void TRPCMethods::RegisterMethod(const std::string& service, const std::string& method, TMethodHandler handler)
{
    Methods[service + "/" + method] = handler;
}

void TRPCMethods::RegisterAsyncMethod(const std::string& service,
                                      const std::string& method,
                                      TAsyncMethodHandler handler)
{
    AsyncMethods[service + "/" + method] = handler;
}

void TRPCMethods::Start()
{}

void TRPCMethods::Stop()
{
    {
        std::lock_guard<std::mutex> lock(Mutex);
        Stopped = true;
    }
    JobAdded.notify_all();
    if (Thread.joinable()) {
        Thread.join();
    }
}

Json::Value TRPCMethods::Call(const std::string& method, const Json::Value& params)
{
    return Methods.at(method)(params);
}

void TRPCMethods::CallAsync(const std::string& method,
                            const Json::Value& params,
                            TResultCallback onResult,
                            TErrorCallback onError)
{
    auto asyncMethod = AsyncMethods.find(method);
    if (asyncMethod != AsyncMethods.end()) {
        asyncMethod->second(params, onResult, onError);
        return;
    }
    auto handler = Methods.at(method);
    std::lock_guard<std::mutex> lock(Mutex);
    if (Stopped) {
        return;
    }
    Jobs.push([handler, params, onResult, onError]() {
        Json::Value result;
        try {
            result = handler(params);
        } catch (const std::exception& e) {
            onError(WBMQTT::E_RPC_SERVER_ERROR, e.what());
            return;
        }
        onResult(result);
    });
    if (!Thread.joinable()) {
        Thread = std::thread([this]() { RunJobs(); });
    }
    JobAdded.notify_all();
}

void TRPCMethods::RunJobs()
{
    std::unique_lock<std::mutex> lock(Mutex);
    while (true) {
        JobAdded.wait(lock, [this]() { return Stopped || !Jobs.empty(); });
        if (Stopped) {
            return;
        }
        auto job = Jobs.front();
        Jobs.pop();
        lock.unlock();
        job();
        lock.lock();
    }
}

ISerialClientTask::TRunResult TBlockingTask::Run(PFeaturePort port,
                                                 TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                                 const std::list<PSerialDevice>& polledDevices)
{
    Started.set_value();
    ReleaseFuture.wait();
    return ISerialClientTask::TRunResult::OK;
}

bool TBlockingTask::WaitStarted(std::chrono::milliseconds timeout)
{
    return StartedFuture.wait_for(timeout) == std::future_status::ready;
}

void TBlockingTask::Release()
{
    if (!Released.exchange(true)) {
        ReleasePromise.set_value();
    }
}

void TAsyncAnswer::SetResult(const Json::Value& result)
{
    Set(result, std::string());
}

void TAsyncAnswer::SetError(const std::string& error)
{
    Set(Json::Value(), error);
}

void TAsyncAnswer::Set(const Json::Value& result, const std::string& error)
{
    if (!Answered.exchange(true)) {
        Answer.set_value({result, error});
    }
}

bool TAsyncAnswer::IsAnswered() const
{
    return Answered;
}

bool TAsyncAnswer::Wait(std::chrono::milliseconds timeout) const
{
    return AnswerFuture.wait_for(timeout) == std::future_status::ready;
}

std::string TAsyncAnswer::Get() const
{
    return Wait(SERVICE_TEST_WAIT_TIMEOUT) ? AnswerFuture.get().second : std::string("no answer");
}

Json::Value TAsyncAnswer::GetResult() const
{
    return Wait(SERVICE_TEST_WAIT_TIMEOUT) ? AnswerFuture.get().first : Json::Value();
}

PAsyncAnswer CallAsync(TRPCMethods& rpc, const std::string& method, const Json::Value& params)
{
    auto answer = std::make_shared<TAsyncAnswer>();
    rpc.CallAsync(
        method,
        params,
        [answer](const Json::Value& result) { answer->SetResult(result); },
        [answer](WBMQTT::TMqttRpcErrorCode, const std::string& error) { answer->SetError(error); });
    return answer;
}

TMakeTask::TMakeTask(PSerialClientTask task): Task(task)
{}

PSerialClientTask TMakeTask::operator()(PSerialDevice, PDeviceParametersCache cache)
{
    Cache = cache;
    return Task;
}

PDeviceParametersCache TMakeTask::GetCache() const
{
    return Cache;
}
