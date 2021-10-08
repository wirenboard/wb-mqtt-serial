#pragma once

#include <string>
#include <vector>
#include <functional>

#include "serial_device.h"

namespace IEC
{
    const uint8_t ACK = 0x06;
    const uint8_t NAK = 0x15;
    const uint8_t ETX = 0x03;
    const uint8_t SOH = 0x01;
    const uint8_t STX = 0x02;
    const uint8_t EOT = 0x04;

    typedef std::function<uint8_t(const uint8_t* buf, size_t size)> TCrcFn;

    void CheckStripEvenParity(uint8_t* buf, size_t nread);

    std::vector<uint8_t> SetEvenParity(const uint8_t* buf, size_t count);

    size_t ReadFrame(TPort&                           port,
                     uint8_t*                         buf,
                     size_t                           count,
                     const std::chrono::microseconds& responseTimeout,
                     const std::chrono::microseconds& frameTimeout,
                     TPort::TFrameCompletePred        frame_complete,
                     const std::string&               logPrefix);

    void WriteBytes(TPort& port, const uint8_t* buf, size_t count, const std::string& logPrefix);
    void WriteBytes(TPort& port, const std::string& str, const std::string& logPrefix);

    std::vector<uint8_t> MakeRequest(const std::string& command, const std::string& commandData, TCrcFn crcFn);
    std::vector<uint8_t> MakeRequest(const std::string& command, TCrcFn crcFn);

    //! A sum of all bytes modulus 0x7F
    uint8_t Get7BitSum(const uint8_t* data, size_t size);
}

/**
 *  Base class for devices with IEC 61107 protocol.
 *  Implements port setup logic as IEC wants 7E2 and checks slave id.
 */
class TIEC61107Device: public TSerialDevice
{
public:
    TIEC61107Device(PDeviceConfig device_config, PPort port, PProtocol protocol);

    void EndSession() override;
    void Prepare() override;

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
                         PPort port,
                         PProtocol protocol,
                         const std::string& logPrefix,
                         IEC::TCrcFn crcFn);

    uint64_t ReadRegister(PRegister reg) override;
    void     WriteRegister(PRegister reg, uint64_t value) override;
    void     EndPollCycle() override;
    void     EndSession() override;
    void     Prepare() override;

protected:
    /**
     * @brief Get string with parameter request for R1 command
     * Examples:
     *    ETOPE(1)
     *    0011223344()
     */
    virtual std::string GetParameterRequest(const TRegister& reg) const = 0;
    virtual uint64_t GetRegisterValue(const TRegister& reg, const std::string& value) = 0;

private:
    IEC::TCrcFn CrcFn;
    std::string LogPrefix;
    std::unordered_map<std::string, std::string> CmdResultCache;

    std::string GetCachedResponse(const std::string& paramAddress);
    void        SwitchToProgMode();
    void        SendPassword();
    void        SendEndSession();
    size_t      ReadFrameProgMode(uint8_t* buffer, size_t size, uint8_t startByte);
    void        WriteBytes(const std::vector<uint8_t>& data);
    void        WriteBytes(const std::string& str);
};
