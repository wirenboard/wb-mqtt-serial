#pragma once

#include <string>
#include <memory>
#include <exception>
#include <unordered_set>
#include <cstdint>

#include "serial_protocol.h"
#include "regformat.h"

// Common protocol base for electricity meters
class TEMProtocol: public TSerialProtocol {
public:
    static const int DefaultTimeoutMs = 1000;

    TEMProtocol(PDeviceConfig device_config, PAbstractSerialPort port);

    void WriteRegister(uint32_t mod, uint32_t address, uint64_t value, RegisterFormat fmt);
    // XXX FIXME: leaky abstraction (need to refactor)
    // Perhaps add 'brightness' register format
    void SetBrightness(uint32_t mod, uint32_t address, uint8_t value);

protected:
    enum ErrorType {
        NO_ERROR,
        NO_OPEN_SESSION,
        OTHER_ERROR
    };
    virtual bool ConnectionSetup(uint8_t slave) = 0;
    virtual ErrorType CheckForException(uint8_t* frame, int len, const char** message) = 0;
    void WriteCommand(uint8_t slave, uint8_t cmd, uint8_t* payload, int len);
    bool ReadResponse(uint8_t slave, int expectedByte1, uint8_t* buf, int len);
    void Talk(uint8_t slave, uint8_t cmd, uint8_t* payload, int payloadLen,
              int expectedByte1, uint8_t* respPayload, int respPayloadLen);
    const int MAX_LEN = 64;
    std::vector<uint8_t> Password() const { return password; }

private:
    void EnsureSlaveConnected(uint8_t slave, bool force = false);

    std::unordered_set<uint8_t> connectedSlaves;
    std::vector<uint8_t> password;
    const int N_CONN_ATTEMPTS = 10;
};

typedef std::shared_ptr<TEMProtocol> PEMProtocol;
