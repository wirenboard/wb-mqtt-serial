#include "energomera_iec_device.h"

#include <string.h>

#include "iec_common.h"
#include "log.h"

#define LOG(logger) logger.Log() << LOG_PREFIX

namespace
{
    const char* LOG_PREFIX = "[Energomera] ";

    class TEnergomeraIecProtocol: public TIECProtocol
    {
    public:
        TEnergomeraIecProtocol()
            : TIECProtocol("energomera_iec", {{ 0, "group_single", "value", Double, true }})
        {}
    };
}

void TEnergomeraIecDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(new TEnergomeraIecProtocol(),
                             new TBasicDeviceFactory<TEnergomeraIecDevice>("#/definitions/simple_device_with_broadcast", 
                                                                           "#/definitions/common_channel"));
}

namespace
{
    const int CodeUnsupportedParameter = 12;
    const int CodeUnsupportedParameterValue = 17;

    const size_t RESPONSE_BUF_LEN = 1000;

    uint16_t GetParamId(const PRegister & reg)
    {
        return ((GetUint32RegisterAddress(reg->GetAddress()) & 0xFFFF00) >> 8) & 0xFFFF;
    }

    uint8_t GetValueNum(const PRegister & reg)
    {
        return GetUint32RegisterAddress(reg->GetAddress()) & 0xFF;
    }

    class TEnergomeraRegisterRange: public TSimpleRegisterRange
    {
    public:
        TEnergomeraRegisterRange(const std::list<PRegister>& regs) : TSimpleRegisterRange(regs) {
            std::list<PRegister> sorted_reg_list = regs;
            sorted_reg_list.sort(
                [](const PRegister& a, const  PRegister& b) -> bool {
                    if (GetParamId(a) < GetParamId(b)) return true;
                    if (GetParamId(a) > GetParamId(b)) return false;

                    if (GetValueNum(a) < GetValueNum(b)) return true;
                    if (GetValueNum(a) > GetValueNum(b)) return false;

                    return false;
                }
            );

            for (auto reg: sorted_reg_list) {
                auto param_id = GetParamId(reg);
                auto value_num = GetValueNum(reg);
                ParamMasks[param_id] |= (1 << (value_num - 1));
                RegsByParam[param_id].push_back(reg);
            };
        };
    public:
        std::map<uint16_t, uint16_t> ParamMasks;

        // x[Param] -> Regs (sorted by bit number)
        std::map<uint16_t, std::list<PRegister> > RegsByParam;
    };

    uint8_t GetChecksum(const uint8_t* data, size_t size)
    {
        uint8_t crc = 0;
        for (size_t i = 0; i < size; ++i) {
            crc = (crc + data[i]) & 0x7F;
        }
        return crc;
    }

    void CheckStripChecksum(uint8_t* resp, size_t len) 
    {
        if (len < 2) {
            throw TSerialDeviceTransientErrorException("empty response");
        }
        uint8_t checksum = GetChecksum(resp+1, len-2);
        if (resp[len-1] != checksum) {
            throw TSerialDeviceTransientErrorException("invalid response checksum (" + std::to_string(resp[len-1]) + " != " + std::to_string(checksum) + ")");
        }

        //replace checksum with null byte to make it C string
        resp[len-1] = '\000';
    }

    size_t ReadFrameFastRead(TPort& port, uint8_t* buf, size_t size, TDeviceConfig& deviceConfig)
    {
        return IEC::ReadFrame(port, buf, size, deviceConfig.ResponseTimeout, deviceConfig.FrameTimeout, 
            [](uint8_t* buf, int size) {
                return ( size > 3  && buf[0] == IEC::STX && buf[size - 2] == IEC::ETX ); // <STX> ... <ETX><BCC>
                    
        }, LOG_PREFIX);
    } 

    void SendFastGroupReadRequest(TPort& port, TEnergomeraRegisterRange& range, const std::string& slaveId)
    {
        // TODO: Exclude unavailable registers
        // request looks like this:
        //  Write [Energomera]:/?00000211!<SOH>R1<STX>GROUP(1001(1)1004(1)1008(1)4001(7))<ETX>	

        char buf[128] = {};
        // 4(header) + 1(crc)
        char cmd_part[sizeof(buf) - 5] = {};
        // 11 bytes of header
        char query_part[sizeof(cmd_part) - 11] = {};
        for (const auto & kv : range.ParamMasks) {
            snprintf(query_part + strlen(query_part), sizeof(query_part) - strlen(query_part), "%04hX(%hX)", kv.first, kv.second); 
        }

        snprintf(cmd_part, sizeof(cmd_part), "R1\x02GROUP(%s)\x03", query_part);
        snprintf(buf, sizeof(buf), "/?%s!\x01%s", slaveId.data(), cmd_part);
        // place checksum in place of trailing null byte
        buf[strlen(buf)] = GetChecksum((uint8_t*) (&cmd_part[0]), strlen(cmd_part));
        IEC::WriteBytes(port, (uint8_t*) buf, strlen(buf), LOG_PREFIX);
    }

    /* @returns: null-terminated char* string which is a part of the input buffer. 
        input buffer is modified in place */
    char* ReadResponse(TPort& port, uint8_t* buf, size_t buf_len, TDeviceConfig& deviceConfig)
    {
        auto len = ReadFrameFastRead(port, buf, buf_len, deviceConfig);
        CheckStripChecksum(buf, len);

        //reply looks like this:
        //  ReadFrame [Energomera]:<STX>1001(0.8797784)1004(1.63211)1008(0.085901)4001(0.135)(0.467)(256.546)<ETX>/

        // Proper response (inc. error) must start with STX, and end with ETX
        if ((buf[0] != IEC::STX) || (buf[len-2] != IEC::ETX)) {
            throw TSerialDeviceTransientErrorException("Malformed response");
        }

        // strip STX and ETX
        buf[len - 2] = '\000';
        char* presp = (char*) buf + 1;
        return presp;
    }

    void ProcessResponse(TEnergomeraRegisterRange& range, char* presp)
    {
        int nread;
        for (const auto & kv : range.RegsByParam) {
            auto & regs = kv.second;
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
                    uint64_t value = 0;
                    static_assert((sizeof(value) >= sizeof(resp_val)), "Can't fit double into uint64_t");
                    memcpy(&value, &resp_val, sizeof(resp_val));
                    reg->SetValue(value);
                } else {
                    // consume error message instead
                    int err_num;
                    ret = sscanf(presp, "(E%d)%n", &err_num, &nread);
                    if (ret >= 1) {
                        reg->SetError(ST_DEVICE_ERROR);
                        presp += nread;

                        // just ignore error message if reg is unavailable
                        if (reg->IsAvailable()) {
                            if (err_num == CodeUnsupportedParameter) {
                                reg->SetAvailable(false);
                                LOG(Warn) << " [slave_id is " << reg->Device()->ToString() + "] Register " << reg->ToString() << " unsupported parameter";
                            } else if (err_num == CodeUnsupportedParameterValue) {
                                reg->SetAvailable(false);
                                LOG(Warn) << " [slave_id is " << reg->Device()->ToString() + "] Register " << reg->ToString() << " unsupported parameter value";
                            } else {
                                LOG(Warn) << " [slave_id is " << reg->Device()->ToString() + "] Register " << reg->ToString() << " error code: " + std::to_string(err_num);
                            }
                        }
                    } else {
                        reg->SetError(ST_UNKNOWN_ERROR);
                        throw TSerialDeviceTransientErrorException("Can't parse response");
                    }
                }
            }
        }
    }
}

TEnergomeraIecDevice::TEnergomeraIecDevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : TIECDevice(config, port, protocol)
{}

std::list<PRegisterRange> TEnergomeraIecDevice::ReadRegisterRange(PRegisterRange abstract_range)
{
    auto range  = std::dynamic_pointer_cast<TEnergomeraRegisterRange>(abstract_range);
    if (!range) {
        throw std::runtime_error("TEnergomeraRegisterRange expected");
    }

    Port()->SkipNoise();
    Port()->CheckPortOpen();

    try {
        SendFastGroupReadRequest(*Port(), *range, SlaveId);

        uint8_t resp[RESPONSE_BUF_LEN] = {};
        char* presp = ReadResponse(*Port(), resp, RESPONSE_BUF_LEN, *DeviceConfig());

        ProcessResponse(*range, presp);
    } catch (TSerialDeviceTransientErrorException& e) {
        range->SetError(ST_UNKNOWN_ERROR);
        auto& logger = GetIsDisconnected() ? Debug : Warn;
        LOG(logger) << "TEnergomeraIecDevice::ReadRegisterRange(): " << e.what() << " [slave_id is " << ToString() + "]";
    }
    return { abstract_range };
}

void TEnergomeraIecDevice::WriteRegister(PRegister, uint64_t)
{
    throw TSerialDeviceException("Energomera protocol: writing register is not supported");
}

std::list<PRegisterRange> TEnergomeraIecDevice::SplitRegisterList(const std::list<PRegister> & reg_list, bool enableHoles) const
{
    std::list<PRegisterRange> r;
    if (reg_list.empty())
        return r;

    std::list<PRegister> sorted_reg_list = reg_list;
    
    sorted_reg_list.sort(
        [](const PRegister& a, const  PRegister& b) -> bool {
            if (a->PollInterval < b->PollInterval) return true;
            if (a->PollInterval > b->PollInterval) return false;
            
            if (GetParamId(a) < GetParamId(b)) return true;
            if (GetParamId(a) > GetParamId(b)) return false;

            return false;
        }
    );

    std::chrono::milliseconds prev_interval(-1);

    std::list<PRegister> cur_range;
    for (auto reg: sorted_reg_list) {
        if ( (reg->PollInterval != prev_interval) || (cur_range.size() > 10)) {
            prev_interval = reg->PollInterval;

            if (!cur_range.empty()) {
                auto range = std::make_shared<TEnergomeraRegisterRange>(cur_range);
                r.push_back(range);
                cur_range.clear();
            }
        }
        cur_range.push_back(reg);
    }

    if (!cur_range.empty()) {
        auto range = std::make_shared<TEnergomeraRegisterRange>(cur_range);
        r.push_back(range);
        cur_range.clear();
    }

    return r;
}
