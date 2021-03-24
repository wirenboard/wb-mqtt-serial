#pragma once

#include "serial_device.h"
#include "serial_config.h"
#include "device_template_generator.h"

#include "GXDLMSSecureClient.h"

const int PUBLIC_CLIENT_ADDRESS = 16;

struct TDlmsDeviceConfig
{
    PDeviceConfig       DeviceConfig;
    int                 LogicalObjectAddress      = 1;
    int                 ClientAddress             = PUBLIC_CLIENT_ADDRESS;

    // Use Logical name referencing by default as it is only option for SPODES
    bool                UseLogicalNameReferencing = true;
    DLMS_SECURITY       Security                  = DLMS_SECURITY_NONE;
    DLMS_AUTHENTICATION Authentication            = DLMS_AUTHENTICATION_NONE;
    DLMS_INTERFACE_TYPE InterfaceType             = DLMS_INTERFACE_TYPE_HDLC;
};

class TDlmsDevice: public TSerialDevice, public TUInt32SlaveId
{
    std::unique_ptr<CGXDLMSSecureClient> Client;

    void InitializeConnection();
    void SendData(const uint8_t* data, size_t size);
    void SendData(const std::string& str);
    void Read(unsigned char eop, CGXByteBuffer& reply);
    void ReadData(CGXByteBuffer& reply);
    void ReadDataBlock(const uint8_t* data, size_t size, CGXReplyData& reply);
    void ReadDLMSPacket(const uint8_t* data, size_t size, CGXReplyData& reply);
    void ReadAttribute(const std::string& addr, int attribute, CGXDLMSObject& obj);
    void GetAssociationView();
    void Disconnect();

    void CheckCycle(std::function<int(std::vector<CGXByteBuffer>&)> requestsGenerator,
                    std::function<int(CGXReplyData&)>               responseParser,
                    const std::string&                              errorMsg);

public:
    TDlmsDevice(const TDlmsDeviceConfig& config, PPort port, PProtocol protocol);

    uint64_t ReadRegister(PRegister reg) override;
    void WriteRegister(PRegister reg, uint64_t value) override;
    void Prepare() override;
    void EndSession() override;

    static void Register(TSerialDeviceFactory& factory);

    const CGXDLMSObjectCollection& ReadAllObjects(bool readAttributes);
    std::map<int, std::string> GetLogicalDevices();
};

namespace DLMS
{
    void PrintDeviceTemplateGenerationOptionsUsage();
    void GenerateDeviceTemplate(TDeviceTemplateGenerationMode   mode, 
                                PPort                           port,
                                const std::string&              phisycalDeviceAddress,
                                const std::string&              destinationDir, 
                                const std::vector<std::string>& options);
}
