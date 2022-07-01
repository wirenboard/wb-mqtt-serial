#include <wblib/json_utils.h>

enum class TRPCModbusMode
{
    RPC_RTU,
    RPC_TCP
};

class TRPCRequest
{
public:
    bool CheckParamSet(const Json::Value& request);
    bool LoadValues(const Json::Value& request);

    enum TRPCModbusMode Mode;
    std::string Path, Ip;
    int PortNumber;
    std::vector<uint8_t> Msg;
    std::chrono::microseconds ResponseTimeout;
    std::chrono::microseconds FrameTimeout;
    std::chrono::seconds TotalTimeout;
    std::string Format;
    size_t ResponseSize;
};

typedef std::shared_ptr<TRPCRequest> PRPCRequest;
