#pragma once

#include "device_template_generator.h"
#include "serial_config.h"
#include "serial_device.h"

#include "GXDLMSSecureClient.h"

const int PUBLIC_CLIENT_ADDRESS = 16;

struct TDlmsDeviceConfig
{
    PDeviceConfig DeviceConfig;
    int LogicalDeviceAddress = 1;
    int ClientAddress = PUBLIC_CLIENT_ADDRESS;

    DLMS_SECURITY Security = DLMS_SECURITY_NONE;
    DLMS_AUTHENTICATION Authentication = DLMS_AUTHENTICATION_NONE;
    DLMS_INTERFACE_TYPE InterfaceType = DLMS_INTERFACE_TYPE_HDLC;
    std::chrono::milliseconds DisconnectRetryTimeout = std::chrono::milliseconds::zero();
};

class TDlmsDevice: public TSerialDevice, public TUInt32SlaveId
{
    std::unique_ptr<CGXDLMSSecureClient> Client;
    std::chrono::milliseconds DisconnectRetryTimeout;

    void InitializeConnection(TPort& port);
    void SendData(TPort& port, const uint8_t* data, size_t size);
    void SendData(TPort& port, const std::string& str);
    void Read(TPort& port, unsigned char eop, CGXByteBuffer& reply);
    void ReadData(TPort& port, CGXByteBuffer& reply);
    void ReadDataBlock(TPort& port, const uint8_t* data, size_t size, CGXReplyData& reply);
    void ReadDLMSPacket(TPort& port, const uint8_t* data, size_t size, CGXReplyData& reply);
    void ReadAttribute(TPort& port, const std::string& addr, int attribute, CGXDLMSObject& obj);
    void GetAssociationView(TPort& port);
    void Disconnect(TPort& port);

    void CheckCycle(TPort& port,
                    std::function<int(std::vector<CGXByteBuffer>&)> requestsGenerator,
                    std::function<int(CGXReplyData&)> responseParser,
                    const std::string& errorMsg);

public:
    TDlmsDevice(const TDlmsDeviceConfig& config, PProtocol protocol);

    void EndSession(TPort& port) override;

    static void Register(TSerialDeviceFactory& factory);

    const CGXDLMSObjectCollection& ReadAllObjects(TPort& port, bool readAttributes);
    std::map<int, std::string> GetLogicalDevices(TPort& port);

protected:
    void PrepareImpl(TPort& port) override;
    TRegisterValue ReadRegisterImpl(TPort& port, const TRegisterConfig& reg) override;
};

namespace DLMS
{
    void PrintDeviceTemplateGenerationOptionsUsage();
    void GenerateDeviceTemplate(TDeviceTemplateGenerationMode mode,
                                PPort port,
                                const std::string& phisycalDeviceAddress,
                                const std::string& destinationDir,
                                const std::vector<std::string>& options);
}
