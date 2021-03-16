#pragma once

#include "serial_device.h"
#include "serial_config.h"

#include "GXDLMSSecureClient.h"

const int PUBLIC_CLIENT_ADDRESS = 16;

struct TDlmsDeviceConfig: public TDeviceConfig
{
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
    std::shared_ptr<TDlmsDeviceConfig>   Config;
    std::unique_ptr<CGXDLMSSecureClient> Client;

    void InitializeConnection();
    void SendData(const uint8_t* data, size_t size);
    void SendData(const std::string& str);
    void SendData(CGXByteBuffer& data);
    void Read(unsigned char eop, CGXByteBuffer& reply);
    int ReadData(CGXByteBuffer& reply);
    int ReadDataBlock(CGXByteBuffer& data, CGXReplyData& reply);
    int ReadDataBlock(std::vector<CGXByteBuffer>& data, CGXReplyData& reply);
    int ReadDLMSPacket(CGXByteBuffer& data, CGXReplyData& reply);
    void ReadAttribute(CGXDLMSObject* obj, const std::string& addr, int attribute);
    void GetAssociationView();
    void Disconnect();

public:
    TDlmsDevice(std::shared_ptr<TDlmsDeviceConfig> config, PPort port, PProtocol protocol);

    uint64_t ReadRegister(PRegister reg) override;
    void WriteRegister(PRegister reg, uint64_t value) override;
    void Prepare() override;
    void EndSession() override;
    // virtual void EndPollCycle();

    void ReadAllObjects() const;

    static void Register(TSerialDeviceFactory& factory);

    const CGXDLMSObjectCollection& ReadAllObjects(bool readAttributes);
    std::map<int, std::string> GetLogicalDevices();
};

struct TObisCodeHint
{
    std::string Description;
    std::string MqttControl;
};

typedef std::unordered_map<std::string, TObisCodeHint> TObisCodeHints;

void Print(const CGXDLMSObjectCollection& objs, bool printAttributes, const TObisCodeHints& obisHints);

Json::Value GenerateDeviceTemplate(const std::string&             name,
                                   int                            auth,
                                   const CGXDLMSObjectCollection& objs,
                                   const TObisCodeHints&          obisHints);