#include "neva_device.h"

#include <string.h>
#include <stddef.h>
#include <unistd.h>

#include "iec_common.h"
#include "log.h"

namespace
{
    const char* LOG_PREFIX = "[NEVA] ";
}

#define LOG(logger) ::logger.Log() << LOG_PREFIX

namespace
{
    const TRegisterTypes RegisterTypes{
        { 0, "obis_cdef",      "value", Double, true },
        { 1, "obis_cdef_pf",   "value", Double, true },
        { 2, "obis_cdef_temp", "value", Double, true },
        { 3, "obis_cdef_1",    "value", Double, true },
        { 4, "obis_cdef_2",    "value", Double, true },
        { 5, "obis_cdef_3",    "value", Double, true },
        { 6, "obis_cdef_4",    "value", Double, true },
        { 7, "obis_cdef_5",    "value", Double, true },
    };

    class TNevaIecProtocol: public TIECProtocol
    {
    public:
        TNevaIecProtocol(): TIECProtocol("neva", RegisterTypes)
        {}
    };

    std::unordered_map<std::string, size_t> RegisterTypeValueIndices = {
        {"obis_cdef",      0},
        {"obis_cdef_pf",   0},
        {"obis_cdef_temp", 0},
        {"obis_cdef_1",    0},
        {"obis_cdef_2",    1},
        {"obis_cdef_3",    2},
        {"obis_cdef_4",    3},
        {"obis_cdef_5",    4},
    };

    const size_t RESPONSE_BUF_LEN = 1000;
    const size_t REQUEST_LEN = 4;
    const ptrdiff_t HEADER_SZ = 3;
    const uint8_t REQUEST_PREFIX = 0x31;
    const uint8_t RESPONSE_PREFIX = 0x3E;

    uint8_t GetXorCRC(const uint8_t* data, size_t size)
    {
        uint8_t crc = 0;
        for (size_t i = 0; i < size; ++i) {
            crc ^= data[i];
        }
        return crc & 0x7F;
    }

    void SetCRC(uint8_t* buffer, size_t size) {
        buffer[size - 1] = GetXorCRC(buffer +1, size - 1);
    }

    void CheckStripCRC(uint8_t* resp, size_t len) 
    {
        if (len < 2) {
            throw TSerialDeviceTransientErrorException("empty response");
        }
        uint8_t checksum = GetXorCRC(resp+1, len-2);
        if (resp[len-1] != checksum) {
            throw TSerialDeviceTransientErrorException("invalid response checksum (" + std::to_string(resp[len-1]) + " != " + std::to_string(checksum) + ")");
        }
        
        //replace crc with null byte to make it C string
        resp[len-1] = '\000';
    }

    size_t ReadFrameCRLF(TPort& port,
                         uint8_t* buf,
                         size_t size,
                         const TDeviceConfig& deviceConfig)
    {
        return IEC::ReadFrame(port, buf, size, deviceConfig.ResponseTimeout, deviceConfig.FrameTimeout,
            [](uint8_t* buf, int size) {
                return size >= 2 && buf[size - 1] == '\n' && buf[size - 2] == '\r';
        }, LOG_PREFIX);
    } 

    // replies are either single-byte ACK, NACK or ends with ETX followed by CRC byte
    size_t ReadFrameProgMode(TPort& port,
                             uint8_t* buffer,
                             size_t size,
                             const TDeviceConfig& deviceConfig,
                             uint8_t startByte = IEC::STX)
    {
        return IEC::ReadFrame(port, buffer, size, deviceConfig.ResponseTimeout, deviceConfig.FrameTimeout,
            [=](uint8_t* buf, int size) {
                return ( size == 1 && buf[size - 1] == IEC::ACK )  || // single-byte ACK
                    ( size == 1 && buf[size - 1] == IEC::NAK )  || // single-byte NAK
                    ( size > 3  && buf[0] == startByte && buf[size - 2] == IEC::ETX ); // <STX> ... <ETX>[CRC]
                    
        }, LOG_PREFIX);
    } 

    void SendPassword(TPort& port, const TDeviceConfig& deviceConfig)
    {
        uint8_t buf[RESPONSE_BUF_LEN] = {};
        std::vector<uint8_t> password = {0x00, 0x00, 0x00, 0x00};
        if (deviceConfig.Password.size()) {
            if (deviceConfig.Password.size() != 4)
                throw TSerialDeviceException("invalid password size (4 bytes expected)");
            password = deviceConfig.Password;
        }
        uint8_t password_buf[16];
        snprintf((char*) password_buf, sizeof(password_buf),
            "\001P1\002(%02X%02X%02X%02X)\003",
            password[0], password[1], password[2], password[3]);
        SetCRC(password_buf, sizeof(password_buf));

        IEC::WriteBytes(port, password_buf, sizeof(password_buf) / sizeof(password_buf[0]), LOG_PREFIX);
        auto nread = ReadFrameProgMode(port, buf, RESPONSE_BUF_LEN, deviceConfig);

        if ((nread != 1) || (buf[0] != IEC::ACK)) {
            throw TSerialDeviceTransientErrorException("cannot authenticate with password");
        }
    }

    void SwitchToProgMode(TPort& port, TDeviceConfig& deviceConfig)
    {
        uint8_t buf[RESPONSE_BUF_LEN] = {};

        // We expect mode C protocol and 9600 baudrate. Send ACK for entering into progamming mode
        IEC::WriteBytes(port, "\006" "051\r\n", LOG_PREFIX);
        auto nread = ReadFrameProgMode(port, buf, RESPONSE_BUF_LEN, deviceConfig, IEC::SOH);
        CheckStripCRC(buf, nread);

        static char expected_reply[] = "\x01" "P0" "\x02" "(00000000)" "\x03";
        
        if (strcmp((const char*) buf, expected_reply) != 0) {
            throw TSerialDeviceTransientErrorException("cannot switch to prog mode: invalid response");
        }
    }

    std::vector<double> GetCachedOBISResponse(TPort& port,
                                              uint32_t address,
                                              const TDeviceConfig& deviceConfig,
                                              std::unordered_map<uint32_t, std::vector<double> >& cmdResultCache)
    {
        auto it = cmdResultCache.find(address);
        if (it != cmdResultCache.end()) {
            return it->second;
        }

        std::vector<double> result = {};

        // Address is 0xCCDDEEFF. Decimal separator in value is ignored (use scale)
        // OBIS value groups
        uint8_t c = (address & 0xFF000000) >> 24;
        uint8_t d = (address & 0x00FF0000) >> 16;
        uint8_t e = (address & 0x0000FF00) >> 8;
        uint8_t f = (address & 0x000000FF);

        uint8_t buf[16] = {};
        snprintf((char*) &buf, sizeof(buf), "\x01R1\x02%02X%02X%02X%02X()\x03", c, d, e, f);
        
        // place XOR-CRC in place of trailing null byte
        buf[sizeof(buf) - 1] = GetXorCRC(&buf[1], sizeof(buf) - 2);

        IEC::WriteBytes(port, buf, sizeof(buf), LOG_PREFIX);

        uint8_t resp[RESPONSE_BUF_LEN] = {};
        auto len = ReadFrameProgMode(port, resp, RESPONSE_BUF_LEN, deviceConfig);
        CheckStripCRC(resp, len);
    
        // Proper response (inc. error) must start with STX, and end with ETX
        if ((resp[0] != IEC::STX) || (resp[len-2] != IEC::ETX)) {
            throw TSerialDeviceTransientErrorException("Malformed response");
        }

        // strip STX and ETX
        resp[len - 2] = '\000';
        char * presp = (char *) resp + 1;

        uint8_t r_c = 0, r_d = 0, r_e = 0, r_f = 0;
        double val1 = 0, val2 = 0, val3 = 0, val4 = 0, val5 = 0;
        int ret = sscanf(presp, "%02hhX%02hhX%02hhX%02hhX(%lf,%lf,%lf,%lf,%lf)", &r_c, &r_d, &r_e, &r_f, &val1, &val2, &val3, &val4, &val5);

        if (ret >= 5) { // OBIS header is found
            // check for closing parenthesis
            if (presp[strlen(presp) - 1] != ')')  throw TSerialDeviceTransientErrorException("Malformed response");
            
            if ( (r_c != c) || (r_d != d) || (r_e != e) || (r_f != f)) {
                throw TSerialDeviceTransientErrorException("response OBIS doesn't match request");
            }

            result.push_back(val1);
            if (ret > 5) result.push_back(val2);
            if (ret > 6) result.push_back(val3);
            if (ret > 7) result.push_back(val4);
            if (ret > 8) result.push_back(val5);

        } else {
            // It is probably a error response. It lacks OBIS part
            char * error_msg = presp;
            // if it contain parentheses, strip them
            if (error_msg[0] == '(') {
                error_msg += 1;
            }
            if (error_msg[strlen(error_msg) - 1] == ')') {
                error_msg[strlen(error_msg) - 1] = '\000';
            }

            throw TSerialDeviceTransientErrorException(error_msg);
        }
        
        return cmdResultCache.insert({address, result}).first->second;
    }
}

void TNevaDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(new TNevaIecProtocol(), 
                             new TBasicDeviceFactory<TNevaDevice>("#/definitions/simple_device_with_broadcast",
                                                                  "#/definitions/common_channel"));
}

TNevaDevice::TNevaDevice(PDeviceConfig device_config, PPort port, PProtocol protocol)
    : TIECDevice(device_config, port, protocol)
{}

void TNevaDevice::Prepare()
{
    TIECDevice::Prepare();
    uint8_t buf[RESPONSE_BUF_LEN] = {};
    size_t retryCount = 5;
    while (true) {
        try {
            Port()->SleepSinceLastInteraction(DeviceConfig()->FrameTimeout);
            Port()->SkipNoise();

            // Send session start request
            IEC::WriteBytes(*Port(), "/?" + SlaveId + "!\r\n", LOG_PREFIX);
            // Pass identification response
            ReadFrameCRLF(*Port(), buf, RESPONSE_BUF_LEN, *DeviceConfig());

            SwitchToProgMode(*Port(), *DeviceConfig());

            SendPassword(*Port(), *DeviceConfig());
            return;
        } catch (const TSerialDeviceTransientErrorException& e) {
            --retryCount;
            if (retryCount == 0) {
                throw;
            }
            LOG(Debug) << "TNevaDevice::Prepare(): " << e.what() << " [slave_id is " << ToString() + "]";
        }
    }
}

void TNevaDevice::EndSession()
{
    // We need to terminate the session so meter won't respond to the data meant for other devices

    char req[] = "\001" "B0" "\003" ; // trailing NUL byte will be replaced by CRC
    SetCRC((uint8_t*) req, sizeof(req));
    IEC::WriteBytes(*Port(), (uint8_t*) req, sizeof(req), LOG_PREFIX);

    // A meter need some time to process the command
    std::this_thread::sleep_for(DeviceConfig()->FrameTimeout);
    TIECDevice::EndSession();
}

void TNevaDevice::EndPollCycle()
{
    CmdResultCache.clear();
    TSerialDevice::EndPollCycle();
}

uint64_t TNevaDevice::ReadRegister(PRegister reg)
{
    auto addr = GetUint32RegisterAddress(reg->GetAddress());
    Port()->SkipNoise();
    Port()->CheckPortOpen();
    auto result = GetCachedOBISResponse(*Port(), addr, *DeviceConfig(), CmdResultCache);

    size_t val_index = RegisterTypeValueIndices[reg->TypeName];
    
    if (result.size() < val_index + 1) {
        throw TSerialDeviceTransientErrorException("not enough data in OBIS response");
    }

    auto val = result[val_index];

    if (reg->TypeName == "obis_cdef_pf") {
        // Y: 0, 1 or 2     (C, L or ?)	YХ.ХХХ
        if (val >= 20) {
            val-=20;
        } else if (val >= 10) {
            val = -(val-10);
        }
    } else if (reg->TypeName == "obis_cdef_temp") {
        if (val >= 100.0) {
            val = -(val - 100.0);
        }
    }

    uint64_t value = 0;
    static_assert((sizeof(value) >= sizeof(val)), "Can't fit double into uint64_t");
    memcpy(&value, &val, sizeof(val));

    return value;
}

void TNevaDevice::WriteRegister(PRegister, uint64_t)
{
    throw TSerialDeviceException("Neva protocol: writing register is not supported");
}
