#include "iec_common.h"

#include <iomanip>
#include <sstream>
#include <string.h>

#include "log.h"
#include "serial_exc.h"
#include "serial_port.h"

namespace IEC
{
    const size_t RESPONSE_BUF_LEN = 1000;

    TPort::TFrameCompletePred GetCRLFPacketPred()
    {
        return [](uint8_t* b, size_t s) { return s >= 2 && b[s - 1] == '\n' && b[s - 2] == '\r'; };
    }

    // replies are either single-byte ACK, NACK or ends with ETX followed by CRC byte
    TPort::TFrameCompletePred GetProgModePacketPred(uint8_t startByte)
    {
        return [=](uint8_t* b, size_t s) {
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
        size_t nread = port.ReadFrame(buf, count, responseTimeout, frameTimeout, frame_complete).Count;
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

    uint8_t Calc7BitCrc(const uint8_t* data, size_t size)
    {
        uint8_t crc = 0;
        for (size_t i = 0; i < size; ++i) {
            crc = (crc + data[i]) & 0x7F;
        }
        return crc;
    }

    uint8_t CalcXorCRC(const uint8_t* data, size_t size)
    {
        uint8_t crc = 0;
        for (size_t i = 0; i < size; ++i) {
            crc ^= data[i];
        }
        return crc & 0x7F;
    }
}

TIEC61107Device::TIEC61107Device(PDeviceConfig device_config, PProtocol protocol)
    : TSerialDevice(device_config, protocol),
      SlaveId(device_config->SlaveId)
{
    if (SlaveId.size() > 32) {
        throw TSerialDeviceException("SlaveId shall be 32 characters maximum");
    }

    if (SlaveId.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890 ") !=
        std::string::npos)
    {
        throw TSerialDeviceException("The characters in SlaveId can only be a...z, A...Z, 0...9 and space");
    }
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
                                           PProtocol protocol,
                                           const std::string& logPrefix,
                                           IEC::TCrcFn crcFn)
    : TIEC61107Device(device_config, protocol),
      CrcFn(crcFn),
      LogPrefix(logPrefix),
      ReadCommand(IEC::UnformattedReadCommand),
      DefaultBaudRate(9600),
      CurrentBaudRate(9600)
{
    SetDesiredBaudRate(9600);
}

void TIEC61107ModeCDevice::PrepareImpl(TPort& port)
{
    TIEC61107Device::PrepareImpl(port);
    bool sessionIsOpen = false;
    try {
        StartSession(port);
        sessionIsOpen = true;
        SwitchToProgMode(port);
        SendPassword(port);
        SetTransferResult(true);
    } catch (const TSerialDeviceTransientErrorException& e) {
        Debug.Log() << LogPrefix << "Session start error: " << e.what() << " [slave_id is " << ToString() + "]";
        if (sessionIsOpen) {
            SendEndSession(port);
        }
        throw;
    }
}

void TIEC61107ModeCDevice::StartSession(TPort& port)
{
    if (DefaultBaudRate != DesiredBaudRate) {
        CurrentBaudRate = DefaultBaudRate;
    }
    TSerialPortConnectionSettings bf(CurrentBaudRate, 'E', 7, 1);
    port.ApplySerialPortSettings(bf);
    if (Probe(port)) {
        return;
    }
    // Try desired baudrate, the device can remember it
    if (DefaultBaudRate != DesiredBaudRate) {
        bf.BaudRate = DesiredBaudRate;
        port.ApplySerialPortSettings(bf);
        if (Probe(port)) {
            CurrentBaudRate = bf.BaudRate;
            return;
        }
    }
    throw TSerialDeviceTransientErrorException("Sign on failed");
}

bool TIEC61107ModeCDevice::Probe(TPort& port)
{
    uint8_t buf[IEC::RESPONSE_BUF_LEN] = {};
    size_t retryCount = 5;
    while (true) {
        try {
            port.SkipNoise();
            // Send session start request
            WriteBytes(port, "/?" + SlaveId + "!\r\n");
            // Pass identification response
            IEC::ReadFrame(port,
                           buf,
                           sizeof(buf),
                           GetResponseTimeout(port),
                           GetFrameTimeout(port),
                           IEC::GetCRLFPacketPred(),
                           LogPrefix);
            return true;
        } catch (const TSerialDeviceTransientErrorException& e) {
            --retryCount;
            SendEndSession(port);
            if (retryCount == 0) {
                return false;
            }
        }
    }
}

void TIEC61107ModeCDevice::EndSession(TPort& port)
{
    SendEndSession(port);
    port.ResetSerialPortSettings(); // Return old port settings
    TIEC61107Device::EndSession(port);
}

void TIEC61107ModeCDevice::InvalidateReadCache()
{
    CmdResultCache.clear();
    TSerialDevice::InvalidateReadCache();
}

TRegisterValue TIEC61107ModeCDevice::ReadRegisterImpl(TPort& port, const TRegisterConfig& reg)
{
    port.CheckPortOpen();
    port.SkipNoise();
    return GetRegisterValue(reg, GetCachedResponse(port, GetParameterRequest(reg)));
}

std::string TIEC61107ModeCDevice::GetCachedResponse(TPort& port, const std::string& paramRequest)
{
    auto it = CmdResultCache.find(paramRequest);
    if (it != CmdResultCache.end()) {
        return it->second;
    }

    WriteBytes(port, IEC::MakeRequest(ReadCommand, paramRequest, CrcFn));

    uint8_t resp[IEC::RESPONSE_BUF_LEN] = {};
    auto len = ReadFrameProgMode(port, resp, sizeof(resp), IEC::STX);
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

void TIEC61107ModeCDevice::SwitchToProgMode(TPort& port)
{
    uint8_t buf[IEC::RESPONSE_BUF_LEN] = {};

    WriteBytes(port, ToProgModeCommand);
    if (CurrentBaudRate != DesiredBaudRate) {
        // Time before response must be more than 20ms according to standard
        // Wait some time to transmit request
        port.SleepSinceLastInteraction(std::chrono::milliseconds(10));
        TSerialPortConnectionSettings bf(DesiredBaudRate, 'E', 7, 1);
        port.ApplySerialPortSettings(bf);
        CurrentBaudRate = DesiredBaudRate;
    }
    ReadFrameProgMode(port, buf, sizeof(buf), IEC::SOH);

    // <SOH>P0<STX>(IDENTIFIER)<ETX>CRC
    if (buf[1] != 'P' || buf[2] != '0' || buf[3] != IEC::STX) {
        throw TSerialDeviceTransientErrorException("cannot switch to prog mode: invalid response");
    }
}

void TIEC61107ModeCDevice::SendPassword(TPort& port)
{
    if (DeviceConfig()->Password.empty()) {
        return;
    }
    uint8_t buf[IEC::RESPONSE_BUF_LEN] = {};
    std::stringstream ss;
    ss << "(";
    for (auto p: DeviceConfig()->Password) {
        ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << int(p);
    }
    ss << ")";
    WriteBytes(port, IEC::MakeRequest("P1", ss.str(), CrcFn));
    auto nread = ReadFrameProgMode(port, buf, sizeof(buf), IEC::STX);

    if ((nread != 1) || (buf[0] != IEC::ACK)) {
        throw TSerialDeviceTransientErrorException("cannot authenticate with password");
    }
}

void TIEC61107ModeCDevice::SendEndSession(TPort& port)
{
    // We need to terminate the session so meter won't respond to the data meant for other devices
    WriteBytes(port, IEC::MakeRequest("B0", CrcFn));

    // A device needs some time to process the command
    std::this_thread::sleep_for(GetFrameTimeout(port));
}

size_t TIEC61107ModeCDevice::ReadFrameProgMode(TPort& port, uint8_t* buf, size_t size, uint8_t startByte)
{
    auto len = IEC::ReadFrame(port,
                              buf,
                              size,
                              GetResponseTimeout(port),
                              GetFrameTimeout(port),
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

void TIEC61107ModeCDevice::WriteBytes(TPort& port, const std::vector<uint8_t>& data)
{
    port.SleepSinceLastInteraction(GetFrameTimeout(port));
    IEC::WriteBytes(port, data.data(), data.size(), LogPrefix);
}

void TIEC61107ModeCDevice::WriteBytes(TPort& port, const std::string& data)
{
    port.SleepSinceLastInteraction(GetFrameTimeout(port));
    IEC::WriteBytes(port, data, LogPrefix);
}

void TIEC61107ModeCDevice::SetReadCommand(const std::string& command)
{
    ReadCommand = command;
}

void TIEC61107ModeCDevice::SetDesiredBaudRate(int baudRate)
{
    const std::unordered_map<int, uint8_t> supportedBaudRates = {{300, '0'},
                                                                 {2400, '3'},
                                                                 {4800, '4'},
                                                                 {9600, '5'},
                                                                 {19200, '6'}};
    auto baudRateCodeIt = supportedBaudRates.find(baudRate);
    if (baudRateCodeIt == supportedBaudRates.end()) {
        throw TSerialDeviceException("Unsupported IEC61107 baud rate: " + std::to_string(baudRate));
    }
    DesiredBaudRate = baudRate;
    ToProgModeCommand = {IEC::ACK, '0', baudRateCodeIt->second, '1', '\r', '\n'};
}

void TIEC61107ModeCDevice::SetDefaultBaudRate(int baudRate)
{
    DefaultBaudRate = baudRate;
    CurrentBaudRate = baudRate;
}
