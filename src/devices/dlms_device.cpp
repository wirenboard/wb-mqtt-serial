#include "dlms_device.h"
#include "log.h"

#include "GXDLMSTranslator.h"
#include "GXDLMSConverter.h"
#include "GXDLMSSapAssignment.h"
#include "GXDLMSObject.h"
#include "GXDLMSObjectFactory.h"

#define LOG(logger) ::logger.Log() << "[DLMS] "

namespace
{
    class TObisRegisterAddress: public IRegisterAddress
    {
        std::string LogicalName;
    public:
        TObisRegisterAddress(const std::string& ln) : LogicalName(ln)
        {
            auto bytes = WBMQTT::StringSplit(ln, ".");
            if (bytes.size() != 6) {
                throw TConfigParserException("Bad OBIS code '" + ln + "'");
            }
            for (const auto& b: bytes) {
                if (!std::all_of(b.cbegin(), b.cend(), ::isdigit)) {
                    throw TConfigParserException("Bad OBIS code '" + ln + "'");
                }
            }
        }

        const std::string& GetLogicalName() const
        {
            return LogicalName;
        }

        std::string ToString() const override
        {
            return LogicalName;
        }

        bool IsLessThan(IRegisterAddress& addr) const
        {
            return LogicalName < addr.ToString();
        }

        IRegisterAddress* CalcNewAddress(uint32_t /*offset*/,
                                         uint32_t /*stride*/,
                                         uint32_t /*registerByteWidth*/,
                                         uint32_t /*addressByteStep*/) const override
        {
            return new TObisRegisterAddress(LogicalName);
        }
    };
    class TDlmsDeviceFactory: public IDeviceFactory
    {
    public:
        TDlmsDeviceFactory(): IDeviceFactory("#/definitions/dlms_parameters")
        {}

        PSerialDevice CreateDevice(const Json::Value&    data,
                                   Json::Value           deviceTemplate,
                                   PProtocol             protocol,
                                   const std::string&    defaultId,
                                   PPortConfig           portConfig) const override
        {
            std::shared_ptr<TDlmsDeviceConfig> cfg = std::make_shared<TDlmsDeviceConfig>();
            TUint32RegisterAddress baseRegisterAddress(0);
            LoadBaseDeviceConfig(cfg.get(),
                                 data,
                                 deviceTemplate,
                                 protocol,
                                 defaultId,
                                 portConfig->RequestDelay,
                                 portConfig->ResponseTimeout,
                                 portConfig->PollInterval,
                                 &baseRegisterAddress,
                                 this);

            WBMQTT::JSON::Get(data, "dlms_client_address", cfg->ClientAddress);
            cfg->Authentication = static_cast<DLMS_AUTHENTICATION>(data.get("dlms_auth", cfg->Authentication).asInt());
            cfg->InterfaceType  = static_cast<DLMS_INTERFACE_TYPE>(data.get("dlms_interface", cfg->InterfaceType).asInt());

            PSerialDevice dev = std::make_shared<TDlmsDevice>(cfg, portConfig->Port, protocol);
            dev->InitSetupItems();
            return dev;
        }

        TRegisterDesc LoadRegisterAddress(const Json::Value&      regCfg,
                                          const IRegisterAddress* deviceBaseAddress,
                                          uint32_t                stride,
                                          uint32_t                registerByteWidth) const override
        {
            TRegisterDesc res;
            res.Address = std::make_shared<TObisRegisterAddress>(regCfg["address"].asString());
            return res;
        }
    };

    std::string GetErrorMessage(int err)
    {
        return std::to_string(err) + ", " + CGXDLMSConverter::GetErrorMessage(err);
    }
}

void TDlmsDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(new TUint32SlaveIdProtocol("dlms", TRegisterTypes({{ 0, "default", "value", Double, true }})), 
                             new TDlmsDeviceFactory());
}

TDlmsDevice::TDlmsDevice(std::shared_ptr<TDlmsDeviceConfig> config, PPort port, PProtocol protocol)
    : TSerialDevice(config, port, protocol),
      TUInt32SlaveId(config->SlaveId),
      Config(config)
{
    if (Config->Password.empty() || Config->Password.back() != 0) {
        // CGXDLMSSecureClient wants a zero-terminated string as a password
        Config->Password.push_back(0);
    }

    int serverAddress = Config->LogicalObjectAddress;
    if (serverAddress < 0x80) {
        if (SlaveId > 0) {
            serverAddress <<= (SlaveId < 0x80 ? 7 : 14);
        }
    } else {
        serverAddress <<= 14;
    }
    serverAddress += SlaveId;

    Client = std::make_unique<CGXDLMSSecureClient>(Config->UseLogicalNameReferencing,
                                                   Config->ClientAddress,
                                                   serverAddress,
                                                   Config->Authentication,
                                                   (const char*)(&Config->Password[0]),
                                                   Config->InterfaceType);
}

void TDlmsDevice::ReadAttribute(CGXDLMSObject* obj, const std::string& addr, int attribute)
{
    int ret;
    std::vector<CGXByteBuffer> data;
    CGXReplyData reply;
    if ((ret = Client->Read(obj, attribute, data)) != 0) {
        throw TSerialDeviceTransientErrorException("Can't create " + addr + ":" + std::to_string(attribute) + " read request: " + GetErrorMessage(ret));
    }
    if ((ret = ReadDataBlock(data, reply)) != 0) {
        throw TSerialDeviceTransientErrorException(addr + ":" + std::to_string(attribute) + " read failed: " + GetErrorMessage(ret));
    }
    if ((ret = Client->UpdateValue(*obj, attribute, reply.GetValue())) != 0) {
        throw TSerialDeviceTransientErrorException("Getting " + addr + ":" + std::to_string(attribute) + " failed: " + GetErrorMessage(ret));
    }
}

uint64_t TDlmsDevice::ReadRegister(PRegister reg)
{
    auto addr = dynamic_cast<TObisRegisterAddress*>(reg->Address.get())->GetLogicalName();
    auto obj = Client->GetObjects().FindByLN(DLMS_OBJECT_TYPE_REGISTER, addr);
    if (!obj) {
        obj = CGXDLMSObjectFactory::CreateObject(DLMS_OBJECT_TYPE_REGISTER, addr);
        if (!obj) {
            throw TSerialDeviceTransientErrorException("Can't create register object");
        }
        Client->GetObjects().push_back(obj);
    }

    bool forceValueRead = true;

    std::vector<int> attributes;
    obj->GetAttributeIndexToRead(false, attributes);
    for (auto pos = attributes.begin(); pos != attributes.end(); ++pos) {
        ReadAttribute(obj, addr, *pos);
        if (*pos == 2) {
            forceValueRead = false;
        }
    }

    // Some devices doesn't set read access, let's force read value 
    if (forceValueRead) {
        ReadAttribute(obj, addr, 2);
    }

    auto r = static_cast<CGXDLMSRegister*>(obj);

    if (!r->GetValue().IsNumber()) {
        TSerialDevicePermanentRegisterException(addr + " value is not a number");
    }

    auto resp_val = r->GetValue().ToDouble();
    uint64_t value = 0;
    memcpy(&value, &resp_val, sizeof(resp_val));
    static_assert((sizeof(value) >= sizeof(resp_val)), "Can't fit double into uint64_t");

    return value;
}

void TDlmsDevice::WriteRegister(PRegister reg, uint64_t value)
{
    throw TSerialDeviceException("DLMS protocol: writing to registers is not supported");
}

void TDlmsDevice::Prepare()
{
    Disconnect();
    InitializeConnection();
}

void TDlmsDevice::Disconnect()
{
    int ret;
    std::vector<CGXByteBuffer> data;
    CGXReplyData reply;
    if (   Client->GetInterfaceType() == DLMS_INTERFACE_TYPE_WRAPPER
        || Client->GetCiphering()->GetSecurity() != DLMS_SECURITY_NONE) {
        if ((ret = Client->ReleaseRequest(data)) != 0 ||
            (ret = ReadDataBlock(data, reply)) != 0) {
            LOG(Warn) << "ReleaseRequest failed: " << GetErrorMessage(ret);
        }
    }

    if ((ret = Client->DisconnectRequest(data, true)) != 0 ||
        (ret = ReadDataBlock(data, reply)) != 0) {
        LOG(Warn) << "DisconnectRequest failed: " << GetErrorMessage(ret);
    }
}

void TDlmsDevice::EndSession()
{
    Disconnect();
}

void TDlmsDevice::InitializeConnection()
{
    int ret = 0;
    LOG(Debug) << "Initialize connection";

    std::vector<CGXByteBuffer> data;
    CGXReplyData reply;
    //Get meter's send and receive buffers size.
    if ((ret = Client->SNRMRequest(data)) != 0 ||
        (ret = ReadDataBlock(data, reply)) != 0 ||
        (ret = Client->ParseUAResponse(reply.GetData())) != 0) {
        throw TSerialDeviceTransientErrorException("SNRMRequest failed: " + GetErrorMessage(ret));
    }
    reply.Clear();
    if ((ret = Client->AARQRequest(data)) != 0) {
        throw TSerialDeviceTransientErrorException("AARQRequest creation failed: " + GetErrorMessage(ret));
    }
    if ((ret = ReadDataBlock(data, reply)) != 0) {
        throw TSerialDeviceTransientErrorException("AARQRequest failed: " + GetErrorMessage(ret));
    }
    if((ret = Client->ParseAAREResponse(reply.GetData())) != 0) {
        if (ret == DLMS_ERROR_CODE_APPLICATION_CONTEXT_NAME_NOT_SUPPORTED) {
            throw TSerialDevicePermanentRegisterException("Logical Name referencing is not supported");
        }
        throw TSerialDeviceTransientErrorException("Bad response to AARQRequest: " + GetErrorMessage(ret));
    }
    reply.Clear();
    // Get challenge if HLS authentication is used.
    if (Client->GetAuthentication() > DLMS_AUTHENTICATION_LOW) {
        if ((ret = Client->GetApplicationAssociationRequest(data)) != 0 ||
            (ret = ReadDataBlock(data, reply)) != 0 ||
            (ret = Client->ParseApplicationAssociationResponse(reply.GetData())) != 0) {
            throw TSerialDeviceTransientErrorException("Authentication failed: " + GetErrorMessage(ret));
        }
    }
    LOG(Debug) << "Connection is initialized";
}

void TDlmsDevice::SendData(const uint8_t* data, size_t size)
{
    Port()->SleepSinceLastInteraction(Config->FrameTimeout + Config->RequestDelay);
    Port()->WriteBytes(data, size);
}

void TDlmsDevice::SendData(const std::string& str)
{
    SendData(reinterpret_cast<const uint8_t*>(str.c_str()), str.size());
}

void TDlmsDevice::SendData(CGXByteBuffer& data)
{
    SendData(data.GetData(), data.GetSize());
}

int TDlmsDevice::ReadDataBlock(CGXByteBuffer& data, CGXReplyData& reply)
{
    //If ther is no data to send.
    if (data.GetSize() == 0) {
        return DLMS_ERROR_CODE_OK;
    }
    int ret;
    CGXByteBuffer bb;
    //Send data.
    if ((ret = ReadDLMSPacket(data, reply)) != DLMS_ERROR_CODE_OK) {
        return ret;
    }
    while (reply.IsMoreData()) {
        bb.Clear();
        if ((ret = Client->ReceiverReady(reply.GetMoreData(), bb)) != 0) {
            return ret;
        }
        if ((ret = ReadDLMSPacket(bb, reply)) != DLMS_ERROR_CODE_OK) {
            return ret;
        }
    }
    return DLMS_ERROR_CODE_OK;
}

int TDlmsDevice::ReadDataBlock(std::vector<CGXByteBuffer>& data, CGXReplyData& reply)
{
    //If ther is no data to send.
    if (data.size() == 0) {
        return DLMS_ERROR_CODE_OK;
    }
    int ret;
    CGXByteBuffer bb;
    //Send data.
    for (std::vector<CGXByteBuffer>::iterator it = data.begin(); it != data.end(); ++it) {
        //Send data.
        if ((ret = ReadDLMSPacket(*it, reply)) != DLMS_ERROR_CODE_OK) {
            return ret;
        }
        while (reply.IsMoreData()) {
            bb.Clear();
            if ((ret = Client->ReceiverReady(reply.GetMoreData(), bb)) != 0) {
                return ret;
            }
            if ((ret = ReadDLMSPacket(bb, reply)) != DLMS_ERROR_CODE_OK) {
                return ret;
            }
        }
    }
    return DLMS_ERROR_CODE_OK;
}

int TDlmsDevice::ReadData(CGXByteBuffer& reply)
{
    Read(0x7E, reply);
    return 0;
}

void TDlmsDevice::Read(unsigned char eop, CGXByteBuffer& reply)
{
    size_t lastReadIndex = 0;
    auto frameCompleteFn = [&](uint8_t* buf, size_t size) {
        if (size > 5) {
            auto pos = size - 1;
            for (; pos != lastReadIndex; --pos) {
                if (buf[pos] == eop) {
                    return true;
                }
            }
            lastReadIndex = size - 1;
        }
        return false;
    };

    //TODO: why 200?
    uint8_t buf[200];
    auto bytesRead = Port()->ReadFrame(buf, sizeof(buf), Config->ResponseTimeout, Config->FrameTimeout, frameCompleteFn);
    reply.Set(buf, bytesRead);
}

int TDlmsDevice::ReadDLMSPacket(CGXByteBuffer& data, CGXReplyData& reply)
{
    int ret;
    CGXByteBuffer bb;
    CGXReplyData notify;
    if (data.GetSize() == 0) {
        return DLMS_ERROR_CODE_OK;
    }
    SendData(data);
    // Loop until whole DLMS packet is received.
    unsigned char pos = 0;
    do {
        if (notify.GetData().GetSize() != 0) {
            //Handle notify.
            if (!notify.IsMoreData()) {
                //Show received push message as XML.
                std::string xml;
                CGXDLMSTranslator t(DLMS_TRANSLATOR_OUTPUT_TYPE_SIMPLE_XML);
                if ((ret = t.DataToXml(notify.GetData(), xml)) != 0) {
                    LOG(Debug) << "DataToXml failed";
                } else {
                    LOG(Debug) << xml.c_str();
                }
                notify.Clear();
            }
            continue;
        }
        if ((ret = ReadData(bb)) != 0) {
            if (ret != DLMS_ERROR_CODE_RECEIVE_FAILED || pos == 3) {
                break;
            }
            ++pos;
            LOG(Warn) << "Data send failed. Try to resend " << pos;
            SendData(data);
        }
    } while ((ret = Client->GetData(bb, reply, notify)) == DLMS_ERROR_CODE_FALSE);
    return ret;
}

std::string AligneName(const std::string& name) {
    return name + std::string(6*4-1 - name.size(), ' ');
}

std::string GetDescription(std::string&          logicalName,
                           CGXDLMSObject*        obj,
                           CGXDLMSConverter&     cnv,
                           const TObisCodeHints& obisHints)
{
    std::string res;
    auto it = obisHints.find(logicalName);
    if (it != obisHints.end() && !it->second.Description.empty()) {
        res = it->second.Description;
    }
    std::vector<std::string> descriptions;
    cnv.GetDescription(logicalName, obj->GetObjectType(), descriptions);
    if (descriptions.empty()) {
        return res;
    }
    if (res.empty()) {
        return descriptions[0];
    }
    return res + " (" + descriptions[0] + ")";
}

void TDlmsDevice::GetAssociationView()
{
    LOG(Debug) << "Get association view ...";
    int ret;
    std::vector<CGXByteBuffer> data;
    CGXReplyData reply;
    if ((ret = Client->GetObjectsRequest(data)) != 0 ||
        (ret = ReadDataBlock(data, reply)) != 0 ||
        (ret = Client->ParseObjects(reply.GetData(), true)) != 0) {
        throw TSerialDeviceTransientErrorException("GetObjects failed " + GetErrorMessage(ret));
    }
}

std::map<int, std::string> TDlmsDevice::GetLogicalDevices()
{
    Disconnect();
    InitializeConnection();
    LOG(Debug) << "Getting SAP Assignment ...";
    auto obj = CGXDLMSObjectFactory::CreateObject(DLMS_OBJECT_TYPE_SAP_ASSIGNMENT);
    if (!obj) {
        throw std::runtime_error("Can't create CGXDLMSSapAssignment object");
    }
    Client->GetObjects().push_back(obj);
    int ret;
    std::vector<CGXByteBuffer> data;
    CGXReplyData reply;
    if ((ret = Client->Read(obj, 2, data)) != 0) {
        throw std::runtime_error("Can't create SAP Assignment attribute read request: " + GetErrorMessage(ret));
    }
    if ((ret = ReadDataBlock(data, reply)) != 0) {
        throw std::runtime_error("SAP Assignment attribute read failed: " + GetErrorMessage(ret));
    }
    if ((ret = Client->UpdateValue(*obj, 2, reply.GetValue())) != 0) {
        throw std::runtime_error("Getting SAP Assignment failed: " + GetErrorMessage(ret));
    }

    auto res = static_cast<CGXDLMSSapAssignment*>(obj)->GetSapAssignmentList();
    Disconnect();
    return res;
}

const CGXDLMSObjectCollection& TDlmsDevice::ReadAllObjects(bool readAttributes)
{
    std::cout << "Getting association view ... " << std::endl;
    Disconnect();
    InitializeConnection();
    GetAssociationView();
    std::cout << "Getting objects ..." << std::endl;
    auto& objs = Client->GetObjects();
    if (readAttributes) {
        for (auto it = objs.begin(); it != objs.end(); ++it) {
            if (   ((*it)->GetObjectType() == DLMS_OBJECT_TYPE_PROFILE_GENERIC)
                || (dynamic_cast<CGXDLMSCustomObject*>((*it)) != NULL)) {
                continue;
            }
            std::vector<int> attributes;
            (*it)->GetAttributeIndexToRead(true, attributes);
            for (auto pos = attributes.begin(); pos != attributes.end(); ++pos) {
                std::vector<CGXByteBuffer> data;
                CGXReplyData reply;
                if (   Client->Read(*it, *pos, data) != 0
                    || ReadDataBlock(data, reply) != 0
                    || Client->UpdateValue(**it, *pos, reply.GetValue()) != 0) {
                        continue;
                }
            }
        }
    }
    EndSession();
    return objs;
}

void Print(const CGXDLMSObjectCollection& objs, bool printAttributes, const TObisCodeHints& obisHints)
{
    CGXDLMSConverter cnv;
    for (auto it = objs.begin(); it != objs.end(); ++it) {
        std::string logicalName;
        (*it)->GetLogicalName(logicalName);

        std::string typeName = CGXDLMSConverter::ToString((*it)->GetObjectType());
        if (WBMQTT::StringStartsWith(typeName, "GXDLMS")) {
            typeName.erase(0, 6);
        }
        std::cout << AligneName(logicalName) << " " << typeName << ", " << GetDescription(logicalName, *it, cnv, obisHints);
        if (printAttributes) {
            if (   ((*it)->GetObjectType() == DLMS_OBJECT_TYPE_PROFILE_GENERIC)
                || (dynamic_cast<CGXDLMSCustomObject*>((*it)) != NULL)) {
                std::cout << " (unsupported by wb-mqtt-serial)" << std::endl;
                continue;
            }
            std::cout << std::endl;
            std::vector<std::string> values;
            (*it)->GetValues(values);
            size_t i = 1;
            for (const auto& v: values) {
                std::cout << "\t" << i << ": " << v << std::endl;
                ++i;
            }
        } else {
            std::cout << std::endl;
        }
    }
}

std::string GetChannelName(const std::string& logicalName, const std::string& description, const TObisCodeHints& obisHints)
{
    auto it = obisHints.find(logicalName);
    if (it != obisHints.end() && !it->second.MqttControl.empty()) {
        return it->second.MqttControl;
    }
    if (description.empty() || (description == "Man. specific")) {
        return logicalName;
    }
    return description;
}

bool IsChannelEnabled(const std::string& logicalName, const TObisCodeHints& obisHints)
{
    auto it = obisHints.find(logicalName);
    return (it != obisHints.end() && !it->second.MqttControl.empty());
}

std::string GetType(CGXDLMSObject* obj)
{
    auto reg = dynamic_cast<CGXDLMSRegister*>(obj);
    switch (reg->GetUnit())
    {
    case 9:  return "temperature";
    case 22: return "power";
    case 23: return "pressure";
    case 25: return "power";
    case 32: return "power_consumption";
    case 33: return "current";
    case 35: return "voltage";
    case 38: return "resistance";
    case 46: return "power_consumption";
    }
    return "value";
}

Json::Value MakeChannelDescription(CGXDLMSObject* obj, CGXDLMSConverter& cnv, const TObisCodeHints& obisHints)
{
    std::string logicalName;
    obj->GetLogicalName(logicalName);

    auto description = GetDescription(logicalName, obj, cnv, obisHints);
    auto channelName = GetChannelName(logicalName, description, obisHints);
    Json::Value res;
    res["name"] = channelName;
    res["reg_type"] = "default";
    res["address"] = logicalName;
    res["type"] = GetType(obj);
    if (!IsChannelEnabled(logicalName, obisHints)) {
        res["enabled"] = false;
    }
    std::cout << AligneName(logicalName) << " " << description << " -> " << channelName << std::endl;
    return res;
}

Json::Value GenerateDeviceTemplate(const std::string&             name,
                                   int                            auth,
                                   const CGXDLMSObjectCollection& objs,
                                   const TObisCodeHints&          obisHints)
{
    CGXDLMSConverter cnv;
    Json::Value res;
    res["device_type"] = name;
    auto& device = res["device"];
    device["name"] = name;
    device["id"] = name;
    device["dlms_auth"] = auth;
    device["protocol"] = "dlms";
    device["response_timeout_ms"] = 1000;
    device["frame_timeout_ms"] = 20;
    device["channels"] = Json::Value(Json::arrayValue);
    auto& channels = device["channels"];
    for (auto it = objs.begin(); it != objs.end(); ++it) {
        if ((*it)->GetObjectType() == DLMS_OBJECT_TYPE_REGISTER) {
            channels.append(MakeChannelDescription(*it, cnv, obisHints));
        }
    }
    return res;
}