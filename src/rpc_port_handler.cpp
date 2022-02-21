#include "rpc_port_handler.h"
#include "serial_exc.h"

void TRPCPortHandler::RPCWrite(const std::vector<uint8_t>& buf,
                               size_t responseSize,
                               std::chrono::milliseconds respTimeout,
                               std::chrono::milliseconds frameTimeout)
{
    const std::lock_guard<std::mutex> lock(RPCMutex);
    RPCWriteData = buf;
    RPCRequestedSize = responseSize;
    RPCRespTimeout = respTimeout;
    RPCFrameTimeout = frameTimeout;
    RPCState = RPCPortState::RPC_WRITE;
    return;
}

bool TRPCPortHandler::RPCRead(std::vector<uint8_t>& buf, size_t& actualSize, bool& error)
{
    bool res = false;
    if (((RPCState == RPCPortState::RPC_READ) || (RPCState == RPCPortState::RPC_ERROR)) && (RPCMutex.try_lock())) {
        if (RPCState == RPCPortState::RPC_READ) {
            buf = RPCReadData;
            actualSize = RPCActualSize;
            error = false;
        } else {
            error = true;
        }
        res = true;
        RPCState = RPCPortState::RPC_IDLE;
        RPCMutex.unlock();
    }
    return res;
}

void TRPCPortHandler::RPCRequestHandling(PPort Port)
{
    if ((RPCState == RPCPortState::RPC_WRITE) && (RPCMutex.try_lock())) {

        try {
            Port->WriteBytes(RPCWriteData);

            uint8_t readData[RPCRequestedSize];
            RPCActualSize = Port->ReadFrame(readData, RPCRequestedSize, RPCRespTimeout, RPCFrameTimeout);

            RPCReadData.clear();
            for (size_t i = 0; i < RPCRequestedSize; i++) {
                RPCReadData.push_back(readData[i]);
            }
            RPCState = RPCPortState::RPC_READ;
        } catch (TSerialDeviceException error) {
            RPCState = RPCPortState::RPC_ERROR;
        }

        RPCMutex.unlock();
    }
}