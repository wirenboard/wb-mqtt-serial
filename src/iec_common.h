#pragma once

#include <functional>
#include <string>
#include <vector>

#include "serial_device.h"

namespace IEC
{
    const uint8_t ACK = 0x06;
    const uint8_t NAK = 0x15;
    const uint8_t ETX = 0x03;
    const uint8_t SOH = 0x01;
    const uint8_t STX = 0x02;
    const uint8_t EOT = 0x04;

    const std::string UnformattedReadCommand = "R1";
    const std::string FormattedReadCommand = "R2";

    typedef std::function<uint8_t(const uint8_t* buf, size_t size)> TCrcFn;

    size_t ReadFrame(TPort& port,
                     uint8_t* buf,
                     size_t count,
                     const std::chrono::microseconds& responseTimeout,
                     const std::chrono::microseconds& frameTimeout,
                     TPort::TFrameCompletePred frame_complete,
                     const std::string& logPrefix);

    void WriteBytes(TPort& port, const uint8_t* buf, size_t count, const std::string& logPrefix);
    void WriteBytes(TPort& port, const std::string& str, const std::string& logPrefix);

    std::vector<uint8_t> MakeRequest(const std::string& command, const std::string& commandData, TCrcFn crcFn);
    std::vector<uint8_t> MakeRequest(const std::string& command, TCrcFn crcFn);

    //! A sum of all bytes modulus 0x7F
    uint8_t Calc7BitCrc(const uint8_t* data, size_t size);

    //! A XOR of all bytes modulus 0x7F
    uint8_t CalcXorCRC(const uint8_t* data, size_t size);
}

/**
 *  Base class for devices with IEC 61107 protocol.
 *  Checks slave id.
 */
class TIEC61107Device: public TSerialDevice
{
public:
    TIEC61107Device(PDeviceConfig device_config, PProtocol protocol);

protected:
    std::string SlaveId;
};

//! Base class for IEC 61107 protocol implementations with string slave ids and broadcast support.
class TIEC61107Protocol: public IProtocol
{
public:
    TIEC61107Protocol(const std::string& name, const TRegisterTypes& reg_types);

    bool IsSameSlaveId(const std::string& id1, const std::string& id2) const override;

    bool SupportsBroadcast() const override;
};

/**
 *  Base class for devices with IEC 61107 mode C protocol.
 *  Implements session management logic and read requests for single parameters.
 */
class TIEC61107ModeCDevice: public TIEC61107Device
{
public:
    TIEC61107ModeCDevice(PDeviceConfig device_config,
                         PProtocol protocol,
                         const std::string& logPrefix,
                         IEC::TCrcFn crcFn);

    void InvalidateReadCache() override;
    void EndSession(TPort& port) override;

    //! Set the read command. Default: "R1"
    void SetReadCommand(const std::string& command);

    //! Set desired baud rate. Default: 9600
    void SetDesiredBaudRate(int baudRate);

    //! Set default baud rate on session start. Default: 9600
    void SetDefaultBaudRate(int baudRate);

protected:
    /**
     * @brief Get string with parameter request for read command
     * Examples:
     *    ETOPE(1)
     *    0011223344()
     */
    virtual std::string GetParameterRequest(const TRegisterConfig& reg) const = 0;

    /**
     * @brief Get register value from response
     *
     * @param reg - register
     * @param value - response string without parameter prefix
     *                Example: 1.8.0(019132.530*kWh) -> (019132.530*kWh)
     */
    virtual TRegisterValue GetRegisterValue(const TRegisterConfig& reg, const std::string& value) = 0;
    void PrepareImpl(TPort& port) override;
    TRegisterValue ReadRegisterImpl(TPort& port, const TRegisterConfig& reg) override;

private:
    IEC::TCrcFn CrcFn;
    std::string LogPrefix;
    std::unordered_map<std::string, std::string> CmdResultCache;
    std::string ReadCommand;
    int DefaultBaudRate;
    int CurrentBaudRate;
    int DesiredBaudRate;
    std::vector<uint8_t> ToProgModeCommand;

    std::string GetCachedResponse(TPort& port, const std::string& paramAddress);
    bool Probe(TPort& port);
    void StartSession(TPort& port);
    void SwitchToProgMode(TPort& port);
    void SendPassword(TPort& port);
    void SendEndSession(TPort& port);
    size_t ReadFrameProgMode(TPort& port, uint8_t* buffer, size_t size, uint8_t startByte);
    void WriteBytes(TPort& port, const std::vector<uint8_t>& data);
    void WriteBytes(TPort& port, const std::string& str);
};
