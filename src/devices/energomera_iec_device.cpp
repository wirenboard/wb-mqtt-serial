#include "energomera_iec_device.h"

#include <string.h>

#include "iec_common.h"
#include "log.h"

#define LOG(logger) logger.Log() << LOG_PREFIX

namespace
{
    const char* LOG_PREFIX = "[Energomera] ";
}

void TEnergomeraIecWithFastReadDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(
        new TIEC61107Protocol("energomera_iec", {{0, "group_single", "value", Double, true}}),
        new TBasicDeviceFactory<TEnergomeraIecWithFastReadDevice>("#/definitions/simple_device_with_broadcast",
                                                                  "#/definitions/common_channel"));
}

namespace
{
    const int CodeUnsupportedParameter = 12;
    const int CodeUnsupportedParameterValue = 17;

    const size_t RESPONSE_BUF_LEN = 1000;

    uint16_t GetParamId(const TRegisterConfig& reg)
    {
        return ((GetUint32RegisterAddress(reg.GetAddress()) & 0xFFFF00) >> 8) & 0xFFFF;
    }

    uint8_t GetValueNum(const TRegisterConfig& reg)
    {
        return GetUint32RegisterAddress(reg.GetAddress()) & 0xFF;
    }

    class TEnergomeraRegisterRange: public TRegisterRange
    {
    public:
        bool Add(TPort& port, PRegister reg, std::chrono::milliseconds pollLimit) override
        {
            // TODO: respect pollLimit
            if (RegisterList().size() > 10) {
                return false;
            }

            if (reg->GetAvailable() != TRegisterAvailability::UNAVAILABLE) {
                if (HasOtherDeviceAndType(reg)) {
                    return false;
                }
                RegisterList().push_back(reg);
            }
            return true;
        }

        void UpdateMasks()
        {
            std::list<PRegister> sortedRegList = RegisterList();
            sortedRegList.sort([](const PRegister& a, const PRegister& b) -> bool {
                if (GetParamId(*a->GetConfig()) < GetParamId(*b->GetConfig()))
                    return true;
                if (GetParamId(*a->GetConfig()) > GetParamId(*b->GetConfig()))
                    return false;

                if (GetValueNum(*a->GetConfig()) < GetValueNum(*b->GetConfig()))
                    return true;
                if (GetValueNum(*a->GetConfig()) > GetValueNum(*b->GetConfig()))
                    return false;

                return false;
            });
            RegisterList().swap(sortedRegList);
            for (auto reg: RegisterList()) {
                auto param_id = GetParamId(*reg->GetConfig());
                auto value_num = GetValueNum(*reg->GetConfig());
                ParamMasks[param_id] |= (1 << (value_num - 1));
                RegsByParam[param_id].push_back(reg);
            };
        }

    public:
        std::map<uint16_t, uint16_t> ParamMasks;

        // x[Param] -> Regs (sorted by bit number)
        std::map<uint16_t, std::list<PRegister>> RegsByParam;
    };

    void CheckStripChecksum(uint8_t* resp, size_t len)
    {
        if (len < 2) {
            throw TSerialDeviceTransientErrorException("empty response");
        }
        uint8_t checksum = IEC::Calc7BitCrc(resp + 1, len - 2);
        if (resp[len - 1] != checksum) {
            throw TSerialDeviceTransientErrorException("invalid response checksum (" + std::to_string(resp[len - 1]) +
                                                       " != " + std::to_string(checksum) + ")");
        }

        // replace checksum with null byte to make it C string
        resp[len - 1] = '\000';
    }

    size_t ReadFrameFastRead(TPort& port, uint8_t* buf, size_t size, TDeviceConfig& deviceConfig)
    {
        return IEC::ReadFrame(
            port,
            buf,
            size,
            deviceConfig.ResponseTimeout,
            deviceConfig.FrameTimeout,
            [](uint8_t* buf, int size) {
                return (size > 3 && buf[0] == IEC::STX && buf[size - 2] == IEC::ETX); // <STX> ... <ETX><BCC>
            },
            LOG_PREFIX);
    }

    void SendFastGroupReadRequest(TPort& port, TEnergomeraRegisterRange& range, const std::string& slaveId)
    {
        // request looks like this:
        //  Write [Energomera]:/?00000211!<SOH>R1<STX>GROUP(1001(1)1004(1)1008(1)4001(7))<ETX>

        char buf[128] = {};
        // 4(header) + 1(crc)
        char cmd_part[sizeof(buf) - 5] = {};
        // 11 bytes of header
        char query_part[sizeof(cmd_part) - 11] = {};
        for (const auto& kv: range.ParamMasks) {
            snprintf(query_part + strlen(query_part),
                     sizeof(query_part) - strlen(query_part),
                     "%04hX(%hX)",
                     kv.first,
                     kv.second);
        }

        snprintf(cmd_part, sizeof(cmd_part), "R1\x02GROUP(%s)\x03", query_part);
        snprintf(buf, sizeof(buf), "/?%s!\x01%s", slaveId.data(), cmd_part);
        // place checksum in place of trailing null byte
        buf[strlen(buf)] = IEC::Calc7BitCrc((uint8_t*)(&cmd_part[0]), strlen(cmd_part));
        IEC::WriteBytes(port, (uint8_t*)buf, strlen(buf), LOG_PREFIX);
    }

    /* @returns: null-terminated char* string which is a part of the input buffer.
        input buffer is modified in place */
    char* ReadResponse(TPort& port, uint8_t* buf, size_t buf_len, TDeviceConfig& deviceConfig)
    {
        auto len = ReadFrameFastRead(port, buf, buf_len, deviceConfig);
        CheckStripChecksum(buf, len);

        // reply looks like this:
        //  ReadFrame [Energomera]:<STX>1001(0.8797784)1004(1.63211)1008(0.085901)4001(0.135)(0.467)(256.546)<ETX>/

        // Proper response (inc. error) must start with STX, and end with ETX
        if ((buf[0] != IEC::STX) || (buf[len - 2] != IEC::ETX)) {
            throw TSerialDeviceTransientErrorException("malformed response");
        }

        // strip STX and ETX
        buf[len - 2] = '\000';
        char* presp = (char*)buf + 1;
        return presp;
    }

    void ProcessResponse(TEnergomeraRegisterRange& range, char* presp)
    {
        int nread;
        for (const auto& kv: range.RegsByParam) {
            auto& regs = kv.second;
            auto param_id = kv.first;

            // consume param
            uint16_t resp_param_id;
            int ret = sscanf(presp, "%04hX%n", &resp_param_id, &nread);
            if (ret < 1) {
                throw TSerialDeviceTransientErrorException("param not found in the response");
            }
            if (param_id != resp_param_id) {
                throw TSerialDeviceTransientErrorException("response param ID doesn't match request");
            }
            presp += nread;

            // consume reg values
            for (const auto& reg: regs) {
                double resp_val;
                ret = sscanf(presp, "(%lf)%n", &resp_val, &nread);
                if (ret >= 1) {
                    presp += nread;
                    reg->SetValue(TRegisterValue{CopyDoubleToUint64(resp_val)});
                } else {
                    // consume error message instead
                    int err_num;
                    ret = sscanf(presp, "(E%d)%n", &err_num, &nread);
                    if (ret >= 1) {
                        reg->SetError(TRegister::TError::ReadError);
                        presp += nread;

                        // just ignore error message if reg is unavailable
                        if (reg->GetAvailable() == TRegisterAvailability::UNKNOWN) {
                            if (err_num == CodeUnsupportedParameter) {
                                reg->SetAvailable(TRegisterAvailability::UNAVAILABLE);
                                LOG(Warn) << " [slave_id is " << reg->Device()->ToString() + "] Register "
                                          << reg->ToString() << " unsupported parameter";
                            } else if (err_num == CodeUnsupportedParameterValue) {
                                reg->SetAvailable(TRegisterAvailability::UNAVAILABLE);
                                LOG(Warn) << " [slave_id is " << reg->Device()->ToString() + "] Register "
                                          << reg->ToString() << " unsupported parameter value";
                            } else {
                                LOG(Warn) << " [slave_id is " << reg->Device()->ToString() + "] Register "
                                          << reg->ToString() << " error code: " + std::to_string(err_num);
                            }
                        }
                    } else {
                        throw TSerialDeviceTransientErrorException("Can't parse response");
                    }
                }
            }
        }
    }
}

TEnergomeraIecWithFastReadDevice::TEnergomeraIecWithFastReadDevice(PDeviceConfig config, PProtocol protocol)
    : TIEC61107Device(config, protocol)
{}

void TEnergomeraIecWithFastReadDevice::ReadRegisterRange(TPort& port, PRegisterRange abstract_range)
{
    auto range = std::dynamic_pointer_cast<TEnergomeraRegisterRange>(abstract_range);
    if (!range) {
        throw std::runtime_error("TEnergomeraRegisterRange expected");
    }

    port.CheckPortOpen();
    port.SkipNoise();

    try {
        range->UpdateMasks();
        SendFastGroupReadRequest(port, *range, SlaveId);

        uint8_t resp[RESPONSE_BUF_LEN] = {};
        char* presp = ReadResponse(port, resp, RESPONSE_BUF_LEN, *DeviceConfig());

        SetTransferResult(true);
        ProcessResponse(*range, presp);
    } catch (const TSerialDeviceException& e) {
        for (auto& r: range->RegisterList()) {
            r->SetError(TRegister::TError::ReadError, e.what());
        }
        auto& logger = (GetConnectionState() == TDeviceConnectionState::DISCONNECTED) ? Debug : Warn;
        LOG(logger) << "TEnergomeraIecWithFastReadDevice::ReadRegisterRange(): " << e.what() << " [slave_id is "
                    << ToString() + "]";
        SetTransferResult(false);
    }
}

PRegisterRange TEnergomeraIecWithFastReadDevice::CreateRegisterRange() const
{
    return std::make_shared<TEnergomeraRegisterRange>();
}

void TEnergomeraIecWithFastReadDevice::PrepareImpl(TPort& port)
{
    TIEC61107Device::PrepareImpl(port);
    TSerialPortConnectionSettings bf(9600, 'E', 7, 1);
    port.ApplySerialPortSettings(bf);
}

void TEnergomeraIecWithFastReadDevice::EndSession(TPort& port)
{
    port.ResetSerialPortSettings(); // Return old port settings
    TIEC61107Device::EndSession(port);
}
