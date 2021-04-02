#include "iec_common.h"

#include <string.h>
#include <sstream>

#include "serial_exc.h"
#include "log.h"

namespace IEC
{
    void DumpASCIIChar(std::stringstream& ss, char c)
    {
        switch (c) {
            case SOH:  ss << "<SOH>"; break;
            case STX:  ss << "<STX>"; break;
            case ETX:  ss << "<ETX>"; break;
            case EOT:  ss << "<EOT>"; break;
            case ACK:  ss << "<ACK>"; break;
            case NAK:  ss << "<NAK>"; break;
            case '\r': ss << "<CR>"; break;
            case '\n': ss << "<LF>"; break;
            default:   ss << c;
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

        static uint8_t table[128] = { LOOK_UP }; 
        #undef LOOK_UP
        #undef P6
        #undef P4
        #undef P2

        return table[byte & 0x7F];
    }

    void CheckStripEvenParity(uint8_t* buf, size_t nread)
    {
        for (size_t i = 0; i < nread; ++i) {
            uint8_t parity = !!(buf[i] & (1<<7));
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

    size_t ReadFrame(TPort& port,
                     uint8_t* buf,
                     size_t count,
                     const std::chrono::microseconds& responseTimeout,
                     const std::chrono::microseconds& frameTimeout,
                     TPort::TFrameCompletePred frame_complete,
                     const std::string& logPrefix)
    {
        size_t nread =  port.ReadFrame(buf, count, responseTimeout, frameTimeout, frame_complete);
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
}

TIECDevice::TIECDevice(PDeviceConfig device_config, PPort port, PProtocol protocol)
    : TSerialDevice(device_config, port, protocol), SlaveId(device_config->SlaveId)
{
    if (SlaveId.size() > 32) {
        throw TSerialDeviceException("SlaveId shall be 32 characters maximum");
    }

    if (SlaveId.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890 ") != std::string::npos) {
        throw TSerialDeviceException("The characters in SlaveId can only be a...z, A...Z, 0...9 and space");
    }
}

void TIECDevice::EndSession()
{
    Port()->SetSerialPortByteFormat(nullptr); // Return old port settings
    TSerialDevice::EndSession();
}

void TIECDevice::Prepare()
{
    TSerialDevice::Prepare();
    TSerialPortByteFormat bf('E', 7, 1);
    Port()->SetSerialPortByteFormat(&bf);
}

TIECProtocol::TIECProtocol(const std::string& name, const TRegisterTypes& reg_types)
    : IProtocol(name, reg_types)
{}

bool TIECProtocol::IsSameSlaveId(const std::string& id1, const std::string& id2) const
{
    // Can be only one device with broadcast address
    if (id1.empty() || id2.empty()) {
        return true;
    }
    return id1 == id2;
}
