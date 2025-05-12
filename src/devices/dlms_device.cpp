#include "dlms_device.h"
#include "common_utils.h"
#include "log.h"

#include <fstream>

#include "GXDLMSConverter.h"
#include "GXDLMSObject.h"
#include "GXDLMSObjectFactory.h"
#include "GXDLMSSapAssignment.h"
#include "GXDLMSTranslator.h"

#define LOG(logger) ::logger.Log() << "[" << DeviceConfig()->Name << " (" << ToString() << ")] "

namespace
{
    const size_t MAX_PACKET_SIZE = 200;

    const auto OBIS_CODE_HINTS_FULL_FILE_PATH = "/usr/share/wb-mqtt-serial/obis-hints.json";

    const std::chrono::milliseconds DEFAULT_RESPONSE_TIMEOUT(1000);
    const std::chrono::milliseconds DEFAULT_FRAME_TIMEOUT(20);

    const std::string MAUFACTURER_SPECIFIC_CODES[] = {"1.128-199.0-199,255.0-255.0-255.0-255",
                                                      "1.0-199,255.128-199,240.0-255.0-255.0-255",
                                                      "1.0-199,255.0-199,255.128-254.0-255.0-255",
                                                      "1.0-199,255.0-199,255.0-255.128-254.0-255",
                                                      "1.0-199,255.0-199,255.0-255.0-255.128-254"};

    struct TObisCodeHint
    {
        std::string Description;
        std::string MqttControl;
    };

    typedef std::unordered_map<std::string, TObisCodeHint> TObisCodeHints;

    class TObisRegisterAddress: public IRegisterAddress
    {
        std::string LogicalName;

    public:
        TObisRegisterAddress(const std::string& ln): LogicalName(ln)
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

        int Compare(const IRegisterAddress& addr) const override
        {
            const auto& a = dynamic_cast<const TObisRegisterAddress&>(addr);
            return LogicalName.compare(a.LogicalName);
        }

        IRegisterAddress* CalcNewAddress(uint32_t /*offset*/,
                                         uint32_t /*stride*/,
                                         uint32_t /*registerByteWidth*/,
                                         uint32_t /*addressByteStep*/) const override
        {
            return new TObisRegisterAddress(LogicalName);
        }
    };

    class TObisRegisterAddressFactory: public IRegisterAddressFactory
    {
        TObisRegisterAddress BaseRegisterAddress;

    public:
        TObisRegisterAddressFactory(): BaseRegisterAddress("0.0.0.0.0.0")
        {}

        TRegisterDesc LoadRegisterAddress(const Json::Value& regCfg,
                                          const IRegisterAddress& deviceBaseAddress,
                                          uint32_t stride,
                                          uint32_t registerByteWidth) const override
        {
            TRegisterDesc res;
            res.Address = std::make_shared<TObisRegisterAddress>(regCfg["address"].asString());
            return res;
        }

        const IRegisterAddress& GetBaseRegisterAddress() const override
        {
            return BaseRegisterAddress;
        }
    };

    class TDlmsDeviceFactory: public IDeviceFactory
    {
    public:
        TDlmsDeviceFactory()
            : IDeviceFactory(std::make_unique<TObisRegisterAddressFactory>(),
                             "#/definitions/dlms_device",
                             "#/definitions/dlms_channel")
        {}

        PSerialDevice CreateDevice(const Json::Value& data,
                                   PDeviceConfig deviceConfig,
                                   PPort port,
                                   PProtocol protocol) const override
        {
            TDlmsDeviceConfig cfg;
            cfg.DeviceConfig = deviceConfig;
            WBMQTT::JSON::Get(data, "dlms_client_address", cfg.ClientAddress);
            cfg.Authentication = static_cast<DLMS_AUTHENTICATION>(data.get("dlms_auth", cfg.Authentication).asInt());
            cfg.InterfaceType = static_cast<DLMS_INTERFACE_TYPE>(data.get("dlms_interface", cfg.InterfaceType).asInt());
            WBMQTT::JSON::Get(data, "dlms_disconnect_retry_timeout_ms", cfg.DisconnectRetryTimeout);

            return std::make_shared<TDlmsDevice>(cfg, port, protocol);
        }
    };

    std::string GetErrorMessage(int err)
    {
        return std::to_string(err) + ", " + CGXDLMSConverter::GetErrorMessage(err);
    }

    std::string AlignName(const std::string& name)
    {
        // OBIS code has following structure X.X.X.X.X.X, where X in [0, 255]
        // So the longest printed code will be 6*4-1 characters
        return name + std::string(6 * 4 - 1 - name.size(), ' ');
    }

    std::string GetDescription(const std::string& logicalName,
                               CGXDLMSObject* obj,
                               CGXDLMSConverter* cnv,
                               const TObisCodeHints& obisHints)
    {
        std::string res;
        auto it = obisHints.find(logicalName);
        if (it != obisHints.end() && !it->second.Description.empty()) {
            res = it->second.Description;
        }
        std::vector<std::string> descriptions;
        auto tmp = logicalName;
        cnv->GetDescription(tmp, obj->GetObjectType(), descriptions);
        if (descriptions.empty()) {
            return res;
        }
        if (res.empty()) {
            return descriptions[0];
        }
        return res + " (" + descriptions[0] + ")";
    }

    bool IsChannelEnabled(const std::string& logicalName, const TObisCodeHints& obisHints)
    {
        auto it = obisHints.find(logicalName);
        return (it != obisHints.end() && !it->second.MqttControl.empty());
    }

    bool IsManufacturerSpecific(const std::string& logicalName)
    {
        CGXStandardObisCodeCollection codes;
        for (auto code: MAUFACTURER_SPECIFIC_CODES) {
            std::string dummy;
            codes.push_back(new CGXStandardObisCode(GXHelpers::Split(code, ".", false), dummy, dummy, dummy));
        }
        bool res = false;
        std::vector<CGXStandardObisCode*> list;
        if (DLMS_ERROR_CODE_OK == codes.Find(logicalName, DLMS_OBJECT_TYPE_NONE, list) && !list.empty()) {
            res = (list.front()->GetDescription() != "Invalid");
        }
        for (auto code: list) {
            delete code;
        }
        return res;
    }

    std::string GetChannelName(const std::string& logicalName,
                               const std::string& description,
                               const TObisCodeHints& obisHints)
    {
        auto it = obisHints.find(logicalName);
        if (it != obisHints.end() && !it->second.MqttControl.empty()) {
            return it->second.MqttControl;
        }
        if (description.empty() || IsManufacturerSpecific(logicalName)) {
            return logicalName;
        }
        return description;
    }

    std::string GetType(CGXDLMSObject* obj)
    {
        auto reg = dynamic_cast<CGXDLMSRegister*>(obj);
        if (!reg) {
            return "value";
        }
        switch (reg->GetUnit()) {
            case 9:
                return "temperature";
            case 22:
                return "power";
            case 23:
                return "pressure";
            case 25:
                return "power";
            case 32:
                return "power_consumption";
            case 33:
                return "current";
            case 35:
                return "voltage";
            case 38:
                return "resistance";
            case 46:
                return "power_consumption";
        }
        return "value";
    }

    struct TChannelDescription
    {
        Json::Value channel;
        std::optional<std::string> title;
    };

    TChannelDescription MakeChannelDescription(CGXDLMSObject* obj,
                                               CGXDLMSConverter* cnv,
                                               const TObisCodeHints& obisHints)
    {
        std::string logicalName;
        obj->GetLogicalName(logicalName);

        std::optional<std::string> title;

        auto description = GetDescription(logicalName, obj, cnv, obisHints);
        auto channelName = GetChannelName(logicalName, description, obisHints);
        if (!util::IsValidMqttTopicString(channelName)) {
            title = channelName;
            channelName = util::ConvertToValidMqttTopicString(channelName);
        }
        Json::Value res;
        res["name"] = channelName;
        res["reg_type"] = "default";
        res["address"] = logicalName;
        res["type"] = GetType(obj);
        if (!IsChannelEnabled(logicalName, obisHints)) {
            res["enabled"] = false;
        }
        std::cout << AlignName(logicalName) << " " << description << " -> " << channelName << std::endl;
        return {res, title};
    }

    TObisCodeHints LoadObisCodeHints()
    {
        TObisCodeHints res;
        try {
            for (auto& c: WBMQTT::JSON::Parse(OBIS_CODE_HINTS_FULL_FILE_PATH)) {
                if (c.isMember("obis")) {
                    res.insert({c["obis"].asString(), {c["description"].asString(), c["control"].asString()}});
                }
            }
        } catch (const std::exception& e) {
            std::cout << e.what() << std::endl;
        }
        return res;
    }

    void Print(const CGXDLMSObjectCollection& objs, bool printAttributes, const TObisCodeHints& obisHints)
    {
        CGXDLMSConverter cnv;
        for (auto obj: objs) {
            std::string logicalName;
            obj->GetLogicalName(logicalName);

            std::string typeName = CGXDLMSConverter::ToString(obj->GetObjectType());
            if (WBMQTT::StringStartsWith(typeName, "GXDLMS")) {
                typeName.erase(0, 6);
            }
            std::cout << AlignName(logicalName) << " " << typeName << ", "
                      << GetDescription(logicalName, obj, &cnv, obisHints);
            if (printAttributes) {
                if ((obj->GetObjectType() == DLMS_OBJECT_TYPE_PROFILE_GENERIC) ||
                    (dynamic_cast<CGXDLMSCustomObject*>(obj) != nullptr))
                {
                    std::cout << " (unsupported by wb-mqtt-serial)" << std::endl;
                    continue;
                }
                std::cout << std::endl;
                std::vector<std::string> values;
                obj->GetValues(values);
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

    Json::Value GenerateDlmsDeviceTemplate(const std::string& name,
                                           const TDlmsDeviceConfig& deviceConfig,
                                           const CGXDLMSObjectCollection& objs,
                                           const TObisCodeHints& obisHints)
    {
        CGXDLMSConverter cnv;
        Json::Value res;
        res["device_type"] = name;
        auto& device = res["device"];
        device["name"] = name;
        device["id"] = name;
        device["dlms_auth"] = deviceConfig.Authentication;
        device["dlms_client_address"] = deviceConfig.ClientAddress;
        if (!deviceConfig.DeviceConfig->Password.empty()) {
            Json::Value ar(Json::arrayValue);
            for (auto b: deviceConfig.DeviceConfig->Password) {
                ar.append(b);
            }
            device["password"] = ar;
        }
        device["protocol"] = "dlms";
        device["response_timeout_ms"] = static_cast<Json::Int>(DEFAULT_RESPONSE_TIMEOUT.count());
        device["frame_timeout_ms"] = static_cast<Json::Int>(DEFAULT_FRAME_TIMEOUT.count());
        device["channels"] = Json::Value(Json::arrayValue);
        auto& channels = device["channels"];
        auto& translations = device["translations"]["en"];
        for (auto obj: objs) {
            if (obj->GetObjectType() == DLMS_OBJECT_TYPE_REGISTER) {
                const auto description = MakeChannelDescription(obj, &cnv, obisHints);
                channels.append(description.channel);
                if (description.title) {
                    translations[description.channel["name"].asString()] = *description.title;
                }
            }
        }
        return res;
    }

    const TObisRegisterAddress& ToTObisRegisterAddress(const TRegisterConfig& reg)
    {
        try {
            return dynamic_cast<const TObisRegisterAddress&>(reg.GetAddress());
        } catch (const std::bad_cast&) {
            throw TSerialDeviceTransientErrorException("Address of " + reg.ToString() +
                                                       " can't be casted to TObisRegisterAddress");
        }
    }
}

void TDlmsDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(
        new TUint32SlaveIdProtocol("dlms", TRegisterTypes({{0, "default", "value", Double, true}})),
        new TDlmsDeviceFactory());
}

TDlmsDevice::TDlmsDevice(const TDlmsDeviceConfig& config, PPort port, PProtocol protocol)
    : TSerialDevice(config.DeviceConfig, port, protocol),
      TUInt32SlaveId(config.DeviceConfig->SlaveId),
      DisconnectRetryTimeout(config.DisconnectRetryTimeout)
{
    auto pwd = config.DeviceConfig->Password;
    if (pwd.empty() || pwd.back() != 0) {
        // CGXDLMSSecureClient wants a zero-terminated string as a password
        pwd.push_back(0);
    }

    int serverAddress = config.LogicalDeviceAddress;
    if (serverAddress < 0x80) {
        if (SlaveId > 0) {
            serverAddress <<= (SlaveId < 0x80 ? 7 : 14);
        }
    } else {
        serverAddress <<= 14;
    }
    serverAddress += SlaveId;

    // Use Logical name referencing as it is only option for SPODES
    Client = std::make_unique<CGXDLMSSecureClient>(true,
                                                   config.ClientAddress,
                                                   serverAddress,
                                                   config.Authentication,
                                                   (const char*)(&pwd[0]),
                                                   config.InterfaceType);
}

void TDlmsDevice::CheckCycle(std::function<int(std::vector<CGXByteBuffer>&)> requestsGenerator,
                             std::function<int(CGXReplyData&)> responseParser,
                             const std::string& errorMsg)
{
    std::vector<CGXByteBuffer> data;
    CGXReplyData reply;
    auto res = requestsGenerator(data);
    if (res != DLMS_ERROR_CODE_OK) {
        throw TSerialDeviceTransientErrorException(errorMsg + ". Can't generate request: " + GetErrorMessage(res));
    }
    for (auto& buf: data) {
        try {
            ReadDataBlock(buf.GetData(), buf.GetSize(), reply);
        } catch (const std::exception& e) {
            throw TSerialDeviceTransientErrorException(errorMsg + ". " + e.what());
        }
    }
    res = responseParser(reply);
    if (res != DLMS_ERROR_CODE_OK) {
        if (res == DLMS_ERROR_CODE_APPLICATION_CONTEXT_NAME_NOT_SUPPORTED) {
            throw TSerialDevicePermanentRegisterException("Logical Name referencing is not supported");
        }
        throw TSerialDeviceTransientErrorException(errorMsg + ". Bad response: " + GetErrorMessage(res));
    }
}

void TDlmsDevice::ReadAttribute(const std::string& addr, int attribute, CGXDLMSObject& obj)
{
    CheckCycle([&](auto& data) { return Client->Read(&obj, attribute, data); },
               [&](auto& reply) { return Client->UpdateValue(obj, attribute, reply.GetValue()); },
               "Getting " + addr + ":" + std::to_string(attribute) + " failed");
}

TRegisterValue TDlmsDevice::ReadRegisterImpl(const TRegisterConfig& reg)
{
    auto addr = ToTObisRegisterAddress(reg).GetLogicalName();
    auto obj = Client->GetObjects().FindByLN(DLMS_OBJECT_TYPE_REGISTER, addr);
    if (!obj) {
        obj = CGXDLMSObjectFactory::CreateObject(DLMS_OBJECT_TYPE_REGISTER, addr);
        if (!obj) {
            throw TSerialDeviceTransientErrorException("Can't create register object");
        }
        Client->GetObjects().push_back(obj);
    }

    bool forceValueRead = true;

    const auto REGISTER_VALUE_ATTRIBUTE_INDEX = 2;
    std::vector<int> attributes;
    obj->GetAttributeIndexToRead(false, attributes);
    for (auto pos: attributes) {
        ReadAttribute(addr, pos, *obj);
        if (pos == REGISTER_VALUE_ATTRIBUTE_INDEX) {
            forceValueRead = false;
        }
    }

    // Some devices doesn't set read access, let's force read value
    if (forceValueRead) {
        ReadAttribute(addr, REGISTER_VALUE_ATTRIBUTE_INDEX, *obj);
    }

    auto r = static_cast<CGXDLMSRegister*>(obj);

    if (!r->GetValue().IsNumber()) {
        throw TSerialDevicePermanentRegisterException(addr + " value is not a number");
    }

    return TRegisterValue{CopyDoubleToUint64(r->GetValue().ToDouble())};
}

void TDlmsDevice::PrepareImpl()
{
    try {
        Disconnect();
    } catch (...) {
        if (DisconnectRetryTimeout == std::chrono::milliseconds::zero()) {
            throw;
        }
        // If device doesn't respond, give it some time of silence on bus and try to disconnect again
        Port()->SleepSinceLastInteraction(DisconnectRetryTimeout);
        Disconnect();
    }
    InitializeConnection();
    SetTransferResult(true);
}

void TDlmsDevice::Disconnect()
{
    if (Client->GetInterfaceType() == DLMS_INTERFACE_TYPE_WRAPPER ||
        Client->GetCiphering()->GetSecurity() != DLMS_SECURITY_NONE)
    {
        try {
            CheckCycle([&](auto& data) { return Client->ReleaseRequest(data); },
                       [&](auto& reply) { return DLMS_ERROR_CODE_OK; },
                       "ReleaseRequest failed");
        } catch (const std::exception& e) {
            LOG(Warn) << e.what();
        }
    }
    CheckCycle([&](auto& data) { return Client->DisconnectRequest(data, true); },
               [&](auto& reply) { return DLMS_ERROR_CODE_OK; },
               "DisconnectRequest failed");
}

void TDlmsDevice::EndSession()
{
    Disconnect();
}

void TDlmsDevice::InitializeConnection()
{
    LOG(Debug) << "Initialize connection";

    // Get meter's send and receive buffers size.
    CheckCycle([&](auto& data) { return Client->SNRMRequest(data); },
               [&](auto& reply) { return Client->ParseUAResponse(reply.GetData()); },
               "SNRMRequest failed");

    try {
        CheckCycle([&](auto& data) { return Client->AARQRequest(data); },
                   [&](auto& reply) { return Client->ParseAAREResponse(reply.GetData()); },
                   "AARQRequest failed");

        // Get challenge if HLS authentication is used.
        if (Client->GetAuthentication() > DLMS_AUTHENTICATION_LOW) {
            CheckCycle([&](auto& data) { return Client->GetApplicationAssociationRequest(data); },
                       [&](auto& reply) { return Client->ParseApplicationAssociationResponse(reply.GetData()); },
                       "Authentication failed");
        }
    } catch (const std::exception&) {
        try {
            Disconnect();
        } catch (...) {
        }
        throw;
    }
    LOG(Debug) << "Connection is initialized";
}

void TDlmsDevice::SendData(const uint8_t* data, size_t size)
{
    Port()->SleepSinceLastInteraction(DeviceConfig()->FrameTimeout + DeviceConfig()->RequestDelay);
    Port()->WriteBytes(data, size);
}

void TDlmsDevice::SendData(const std::string& str)
{
    SendData(reinterpret_cast<const uint8_t*>(str.c_str()), str.size());
}

void TDlmsDevice::ReadDataBlock(const uint8_t* data, size_t size, CGXReplyData& reply)
{
    if (size == 0) {
        return;
    }
    ReadDLMSPacket(data, size, reply);
    while (reply.IsMoreData()) {
        int ret;
        CGXByteBuffer bb;
        if ((ret = Client->ReceiverReady(reply.GetMoreData(), bb)) != 0) {
            throw std::runtime_error("Read block failed: " + GetErrorMessage(ret));
        }
        ReadDLMSPacket(bb.GetData(), bb.GetSize(), reply);
    }
}

void TDlmsDevice::ReadData(CGXByteBuffer& reply)
{
    Read(0x7E, reply);
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

    uint8_t buf[MAX_PACKET_SIZE];
    auto bytesRead = Port()
                         ->ReadFrame(buf,
                                     sizeof(buf),
                                     DeviceConfig()->ResponseTimeout,
                                     DeviceConfig()->FrameTimeout,
                                     frameCompleteFn)
                         .Count;
    reply.Set(buf, bytesRead);
}

void TDlmsDevice::ReadDLMSPacket(const uint8_t* data, size_t size, CGXReplyData& reply)
{
    if (size == 0) {
        return;
    }
    SendData(data, size);
    int ret = DLMS_ERROR_CODE_FALSE;
    CGXByteBuffer bb;
    // Loop until whole DLMS packet is received.
    while (ret == DLMS_ERROR_CODE_FALSE) {
        ReadData(bb);
        ret = Client->GetData(bb, reply);
    }
    if (ret != DLMS_ERROR_CODE_OK) {
        throw std::runtime_error("Read DLMS packet failed: " + GetErrorMessage(ret));
    }
}

void TDlmsDevice::GetAssociationView()
{
    LOG(Debug) << "Get association view ...";
    CheckCycle([&](auto& data) { return Client->GetObjectsRequest(data); },
               [&](auto& reply) { return Client->ParseObjects(reply.GetData(), true); },
               "Getting objects from association view failed");
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
    const auto SAP_ASSIGNEMENT_LIST_ATTRIBUTE_INDEX = 2;
    CheckCycle(
        [&](auto& data) { return Client->Read(obj, SAP_ASSIGNEMENT_LIST_ATTRIBUTE_INDEX, data); },
        [&](auto& reply) { return Client->UpdateValue(*obj, SAP_ASSIGNEMENT_LIST_ATTRIBUTE_INDEX, reply.GetValue()); },
        "SAP Assignment attribute read failed");
    auto res = static_cast<CGXDLMSSapAssignment*>(obj)->GetSapAssignmentList();
    Disconnect();
    return res;
}

const CGXDLMSObjectCollection& TDlmsDevice::ReadAllObjects(bool readAttributes)
{
    LOG(Debug) << "Getting association view ... ";
    Disconnect();
    InitializeConnection();
    GetAssociationView();
    LOG(Debug) << "Getting objects ...";
    auto& objs = Client->GetObjects();
    if (readAttributes) {
        for (auto obj: objs) {
            if ((obj->GetObjectType() == DLMS_OBJECT_TYPE_PROFILE_GENERIC) ||
                (dynamic_cast<CGXDLMSCustomObject*>(obj) != nullptr))
            {
                continue;
            }
            std::vector<int> attributes;
            obj->GetAttributeIndexToRead(true, attributes);
            for (auto pos: attributes) {
                try {
                    CheckCycle([&](auto& data) { return Client->Read(obj, pos, data); },
                               [&](auto& reply) { return Client->UpdateValue(*obj, pos, reply.GetValue()); },
                               "");
                } catch (...) {
                }
            }
        }
    }
    EndSession();
    return objs;
}

void DLMS::PrintDeviceTemplateGenerationOptionsUsage()
{
    std::cout << "dlms_hdlc protocol options:" << std::endl
              << "  - client address, typical values:" << std::endl
              << "      16 - public client" << std::endl
              << "      32 - meterings reader" << std::endl
              << "      48 - configuration tool" << std::endl
              << "  - authentication mechanism:" << std::endl
              << "     lowest" << std::endl
              << "     low" << std::endl
              << "     high" << std::endl
              << "     MD5" << std::endl
              << "     SHA1" << std::endl
              << "     GMAC" << std::endl
              << "     SHA256" << std::endl
              << "     ECDSA" << std::endl
              << "  - password" << std::endl;
}

void DLMS::GenerateDeviceTemplate(TDeviceTemplateGenerationMode mode,
                                  PPort port,
                                  const std::string& phisycalDeviceAddress,
                                  const std::string& destinationDir,
                                  const std::vector<std::string>& options)
{
    TDlmsDeviceConfig deviceConfig;
    deviceConfig.DeviceConfig = std::make_shared<TDeviceConfig>();

    if (options.size() > 0) {
        deviceConfig.ClientAddress = atoi(options[0].c_str());
    }

    if (options.size() > 1) {
        const std::unordered_map<std::string, DLMS_AUTHENTICATION> auths = {{"lowest", DLMS_AUTHENTICATION_NONE},
                                                                            {"low", DLMS_AUTHENTICATION_LOW},
                                                                            {"high", DLMS_AUTHENTICATION_HIGH},
                                                                            {"MD5", DLMS_AUTHENTICATION_HIGH_MD5},
                                                                            {"SHA1", DLMS_AUTHENTICATION_HIGH_SHA1},
                                                                            {"GMAC", DLMS_AUTHENTICATION_HIGH_GMAC},
                                                                            {"SHA256", DLMS_AUTHENTICATION_HIGH_SHA256},
                                                                            {"ECDSA", DLMS_AUTHENTICATION_HIGH_ECDSA}};
        auto modeIt = auths.find(options[1]);
        if (modeIt == auths.end()) {
            throw std::runtime_error("Unknown authentication mode: " + options[1]);
        }
        deviceConfig.Authentication = modeIt->second;
    }

    if (options.size() > 2) {
        deviceConfig.DeviceConfig->Password.insert(deviceConfig.DeviceConfig->Password.begin(),
                                                   options[2].begin(),
                                                   options[2].end());
    }

    deviceConfig.DeviceConfig->SlaveId = phisycalDeviceAddress;
    deviceConfig.DeviceConfig->ResponseTimeout = DEFAULT_RESPONSE_TIMEOUT;
    deviceConfig.DeviceConfig->FrameTimeout = DEFAULT_FRAME_TIMEOUT;

    TSerialDeviceFactory deviceFactory;
    TDlmsDevice::Register(deviceFactory);
    port->Open();
    TDlmsDevice device(deviceConfig, port, deviceFactory.GetProtocol("dlms"));
    std::cout << "Getting logical devices..." << std::endl;
    auto logicalDevices = device.GetLogicalDevices();
    std::cout << "Logical devices:" << std::endl;
    for (const auto& ld: logicalDevices) {
        std::cout << ld.first << ": " << ld.second << std::endl;
    }
    std::cout << std::endl;
    for (const auto& ld: logicalDevices) {
        std::cout << "Getting objects for " << ld.second << " ..." << std::endl;
        deviceConfig.LogicalDeviceAddress = ld.first;
        TDlmsDevice d(deviceConfig, port, deviceFactory.GetProtocol("dlms"));
        auto objs = d.ReadAllObjects(mode != 0);
        TObisCodeHints obisHints = LoadObisCodeHints();
        switch (mode) {
            case PRINT: {
                Print(objs, false, obisHints);
                break;
            }
            case VERBOSE_PRINT: {
                Print(objs, true, obisHints);
                break;
            }
            case GENERATE_TEMPLATE: {
                Json::StreamWriterBuilder builder;
                builder["indentation"] = "  ";
                std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
                auto templateFile = destinationDir + "/" + ld.second + ".conf";
                std::ofstream f(templateFile);
                writer->write(GenerateDlmsDeviceTemplate(ld.second, deviceConfig, objs, obisHints), &f);
                std::cout << templateFile << " is generated" << std::endl;
                break;
            }
            case MAX_DEVICE_TEMPLATE_GENERATION_MODE: {
                // Do nothing as calling code should not pass the value
                break;
            }
        }
    }
}
