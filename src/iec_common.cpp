#include "iec_common.h"

#include <iomanip>
#include <sstream>
#include <string.h>

#include "log.h"
#include "serial_exc.h"

namespace IEC
{
    const size_t RESPONSE_BUF_LEN = 1000;

    TPort::TFrameCompletePred GetCRLFPacketPred()
    {
        return [](uint8_t* b, int s) { return s >= 2 && b[s - 1] == '\n' && b[s - 2] == '\r'; };
    }

    // replies are either single-byte ACK, NACK or ends with ETX followed by CRC byte
    TPort::TFrameCompletePred GetProgModePacketPred(uint8_t startByte)
    {
        return [=](uint8_t* b, int s) {
            return (s == 1 && b[s - 1] == IEC::ACK) ||                   // single-byte ACK
                   (s == 1 && b[s - 1] == IEC::NAK) ||                   // single-byte NAK
                   (s > 3 && b[0] == startByte && b[s - 2] == IEC::ETX); // <STX> ... <ETX>[CRC]
        };
    }

    void DumpASCIIChar(std::stringstream& ss, char c)
    {
        switch (c) {
            case SOH:
                ss << "<SOH>";
                break;
            case STX:
                ss << "<STX>";
                break;
            case ETX:
                ss << "<ETX>";
                break;
            case EOT:
                ss << "<EOT>";
                break;
            case ACK:
                ss << "<ACK>";
                break;
            case NAK:
                ss << "<NAK>";
                break;
            case '\r':
                ss << "<CR>";
                break;
            case '\n':
                ss << "<LF>";
                break;
            default: {
                if (isprint(c)) {
                    ss << c;
                } else {
                    ss << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (0xFF & c);
                }
            }
        }
    }

    std::string ToString(const uint8_t* buf, size_t nread)
    {
        std::stringstream ss;
        for (size_t i = 0; i < nread; ++i) {
            DumpASCIIChar(ss, buf[i]);
        }
        return ss.str();
    }

    uint8_t GetParityBit(uint8_t byte)
    {
// Even parity bit
// Generating the look-up table while pre-processing
#define P2(n) n, n ^ 1, n ^ 1, n
#define P4(n) P2(n), P2(n ^ 1), P2(n ^ 1), P2(n)
#define P6(n) P4(n), P4(n ^ 1), P4(n ^ 1), P4(n)
#define LOOK_UP P6(0), P6(1)

        static uint8_t table[128] = {LOOK_UP};
#undef LOOK_UP
#undef P6
#undef P4
#undef P2

        return table[byte & 0x7F];
    }

    void CheckStripEvenParity(uint8_t* buf, size_t nread)
    {
        for (size_t i = 0; i < nread; ++i) {
            uint8_t parity = !!(buf[i] & (1 << 7));
            if (parity != GetParityBit(buf[i])) {
                throw TSerialDeviceTransientErrorException("parity error " + std::to_string(buf[i]));
            }
            buf[i] &= 0x7F;
        }
    }

    /* Writes 7E data in 8N mode appending parity bit.*/
    std::vector<uint8_t> SetEvenParity(const uint8_t* buf, size_t count)
    {
        std::vector<uint8_t> buf_8bit(count);
        for (size_t i = 0; i < count; ++i) {
            buf_8bit[i] = buf[i] | (GetParityBit(buf[i]) << 7);
        }
        return buf_8bit;
    }

    std::vector<uint8_t>& operator<<(std::vector<uint8_t>& v, uint8_t value)
    {
        v.push_back(value);
        return v;
    }

    std::vector<uint8_t>& operator<<(std::vector<uint8_t>& v, const std::string& value)
    {
        std::copy(value.begin(), value.end(), std::back_inserter<std::vector<uint8_t>>(v));
        return v;
    }

    std::vector<uint8_t> MakeRequest(const std::string& command, const std::string& commandData, TCrcFn crcFn)
    {
        std::vector<uint8_t> res;
        res << SOH << command << STX << commandData << ETX << crcFn(&res[1], res.size() - 1);
        return res;
    }

    std::vector<uint8_t> MakeRequest(const std::string& command, TCrcFn crcFn)
    {
        std::vector<uint8_t> res;
        res << SOH << command << ETX << crcFn(&res[1], res.size() - 1);
        return res;
    }

    size_t ReadFrame(TPort& port,
                     uint8_t* buf,
                     size_t count,
                     const std::chrono::microseconds& responseTimeout,
                     const std::chrono::microseconds& frameTimeout,
                     TPort::TFrameCompletePred frame_complete,
                     const std::string& logPrefix)
    {
        size_t nread = port.ReadFrame(buf, count, responseTimeout, frameTimeout, frame_complete);
        if (Debug.IsEnabled()) {
            Debug.Log() << logPrefix << "ReadFrame: " << ToString(buf, nread);
        }
        return nread;
    }

    void WriteBytes(TPort& port, const uint8_t* buf, size_t count, const std::string& logPrefix)
    {
        if (Debug.IsEnabled()) {
            Debug.Log() << logPrefix << "Write: " << ToString(buf, count);
        }
        port.WriteBytes(buf, count);
    }

    void WriteBytes(TPort& port, const std::string& str, const std::string& logPrefix)
    {
        WriteBytes(port, (const uint8_t*)str.data(), str.size(), logPrefix);
    }

    uint8_t Get7BitSum(const uint8_t* data, size_t size)
    {
        uint8_t crc = 0;
        for (size_t i = 0; i < size; ++i) {
            crc = (crc + data[i]) & 0x7F;
        }
        return crc;
    }
}

TIEC61107Device::TIEC61107Device(PDeviceConfig device_config, PPort port, PProtocol protocol)
    : TSerialDevice(device_config, port, protocol),
      SlaveId(device_config->SlaveId)
{
    if (SlaveId.size() > 32) {
        throw TSerialDeviceException("SlaveId shall be 32 characters maximum");
    }

    if (SlaveId.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890 ") !=
        std::string::npos) {
        throw TSerialDeviceException("The characters in SlaveId can only be a...z, A...Z, 0...9 and space");
    }
}

void TIEC61107Device::EndSession()
{
    Port()->SetSerialPortByteFormat(nullptr); // Return old port settings
    TSerialDevice::EndSession();
}

void TIEC61107Device::PrepareImpl()
{
    TSerialDevice::PrepareImpl();
    TSerialPortByteFormat bf('E', 7, 1);
    Port()->SetSerialPortByteFormat(&bf);
}

TIEC61107Protocol::TIEC61107Protocol(const std::string& name, const TRegisterTypes& reg_types)
    : IProtocol(name, reg_types)
{}

bool TIEC61107Protocol::IsSameSlaveId(const std::string& id1, const std::string& id2) const
{
    // Can be only one device with broadcast address
    if (id1.empty() || id2.empty()) {
        return true;
    }
    return id1 == id2;
}

bool TIEC61107Protocol::SupportsBroadcast() const
{
    return true;
}

TIEC61107ModeCDevice::TIEC61107ModeCDevice(PDeviceConfig device_config,
                                           PPort port,
                                           PProtocol protocol,
                                           const std::string& logPrefix,
                                           IEC::TCrcFn crcFn)
    : TIEC61107Device(device_config, port, protocol),
      CrcFn(crcFn),
      LogPrefix(logPrefix)
{}

void TIEC61107ModeCDevice::PrepareImpl()
{
    TIEC61107Device::PrepareImpl();
    uint8_t buf[IEC::RESPONSE_BUF_LEN] = {};
    size_t retryCount = 5;
    bool sessionIsOpen;
    while (true) {
        try {
            sessionIsOpen = false;
            Port()->SkipNoise();

            // Send session start request
            WriteBytes("/?" + SlaveId + "!\r\n");
            // Pass identification response
            IEC::ReadFrame(*Port(),
                           buf,
                           sizeof(buf),
                           DeviceConfig()->ResponseTimeout,
                           DeviceConfig()->FrameTimeout,
                           IEC::GetCRLFPacketPred(),
                           LogPrefix);
            sessionIsOpen = true;
            SwitchToProgMode();
            SendPassword();
            SetTransferResult(true);
            return;
        } catch (const TSerialDeviceTransientErrorException& e) {
            Debug.Log() << LogPrefix << "Session start error: " << e.what() << " [slave_id is " << ToString() + "]";
            if (sessionIsOpen) {
                SendEndSession();
            }
            --retryCount;
            if (retryCount == 0) {
                throw;
            }
        }
    }
}

void TIEC61107ModeCDevice::EndSession()
{
    SendEndSession();
    TIEC61107Device::EndSession();
}

void TIEC61107ModeCDevice::InvalidateReadCache()
{
    CmdResultCache.clear();
    TSerialDevice::InvalidateReadCache();
}

TRegisterValue TIEC61107ModeCDevice::ReadRegisterImpl(PRegister reg)
{
    Port()->SkipNoise();
    Port()->CheckPortOpen();
    return GetRegisterValue(*reg, GetCachedResponse(GetParameterRequest(*reg)));
}

std::string TIEC61107ModeCDevice::GetCachedResponse(const std::string& paramRequest)
{
    auto it = CmdResultCache.find(paramRequest);
    if (it != CmdResultCache.end()) {
        return it->second;
    }

    WriteBytes(IEC::MakeRequest("R1", paramRequest, CrcFn));

    uint8_t resp[IEC::RESPONSE_BUF_LEN] = {};
    auto len = ReadFrameProgMode(resp, sizeof(resp), IEC::STX);
    // Proper response (inc. error) must start with STX, and end with ETX
    if ((resp[0] != IEC::STX) || (resp[len - 2] != IEC::ETX)) {
        throw TSerialDeviceTransientErrorException("malformed response");
    }

    // strip STX and ETX
    resp[len - 2] = '\000';
    char* presp = (char*)resp + 1;

    // parameter name is the a part of a request before '('
    std::string paramName(paramRequest.substr(0, paramRequest.find('(')));

    // Check that response starts from requested parameter name
    if (memcmp(presp, paramName.data(), paramName.size()) == 0) {
        std::string data(presp + paramName.size(), len - 3 - paramName.size());
        CmdResultCache.insert({paramRequest, data});
        return data;
    }
    if (presp[0] != '(') {
        throw TSerialDeviceTransientErrorException("response parameter address doesn't match request");
    }
    // It is probably a error response. It lacks parameter name part
    presp += 1;
    if (presp[strlen(presp) - 1] == ')') {
        presp[strlen(presp) - 1] = '\000';
    }
    throw TSerialDeviceTransientErrorException(presp);
}

void TIEC61107ModeCDevice::SwitchToProgMode()
{
    uint8_t buf[IEC::RESPONSE_BUF_LEN] = {};

    // We expect mode C protocol and 9600 baudrate. Send ACK for entering into progamming mode
    WriteBytes("\006"
               "051\r\n");
    ReadFrameProgMode(buf, sizeof(buf), IEC::SOH);

    // <SOH>P0<STX>(IDENTIFIER)<ETX>CRC
    if (buf[1] != 'P' || buf[2] != '0' || buf[3] != IEC::STX) {
        throw TSerialDeviceTransientErrorException("cannot switch to prog mode: invalid response");
    }
}

void TIEC61107ModeCDevice::SendPassword()
{
    uint8_t buf[IEC::RESPONSE_BUF_LEN] = {};
    std::vector<uint8_t> password = {0x00, 0x00, 0x00, 0x00};
    if (DeviceConfig()->Password.size()) {
        password = DeviceConfig()->Password;
    }
    std::stringstream ss;
    ss << "(";
    for (auto p: password) {
        ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << int(p);
    }
    ss << ")";
    WriteBytes(IEC::MakeRequest("P1", ss.str(), CrcFn));
    auto nread = ReadFrameProgMode(buf, sizeof(buf), IEC::STX);

    if ((nread != 1) || (buf[0] != IEC::ACK)) {
        throw TSerialDeviceTransientErrorException("cannot authenticate with password");
    }
}

void TIEC61107ModeCDevice::SendEndSession()
{
    // We need to terminate the session so meter won't respond to the data meant for other devices
    WriteBytes(IEC::MakeRequest("B0", CrcFn));

    // A device needs some time to process the command
    std::this_thread::sleep_for(DeviceConfig()->FrameTimeout);
}

size_t TIEC61107ModeCDevice::ReadFrameProgMode(uint8_t* buf, size_t size, uint8_t startByte)
{
    auto len = IEC::ReadFrame(*Port(),
                              buf,
                              size,
                              DeviceConfig()->ResponseTimeout,
                              DeviceConfig()->FrameTimeout,
                              IEC::GetProgModePacketPred(startByte),
                              LogPrefix);

    if ((len == 1) && (buf[0] == IEC::ACK || buf[0] == IEC::NAK)) {
        return len;
    }
    if (len < 2) {
        throw TSerialDeviceTransientErrorException("empty response");
    }
    uint8_t checksum = CrcFn(buf + 1, len - 2);
    if (buf[len - 1] != checksum) {
        throw TSerialDeviceTransientErrorException("invalid response checksum (" + std::to_string(buf[len - 1]) +
                                                   " != " + std::to_string(checksum) + ")");
    }
    // replace crc with null byte to make it C string
    buf[len - 1] = '\000';
    return len;
}

void TIEC61107ModeCDevice::WriteBytes(const std::vector<uint8_t>& data)
{
    Port()->SleepSinceLastInteraction(DeviceConfig()->FrameTimeout);
    IEC::WriteBytes(*Port(), data.data(), data.size(), LogPrefix);
}

void TIEC61107ModeCDevice::WriteBytes(const std::string& data)
{
    Port()->SleepSinceLastInteraction(DeviceConfig()->FrameTimeout);
    IEC::WriteBytes(*Port(), data, LogPrefix);
}
