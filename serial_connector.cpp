#include <unistd.h>

#include "serial_connector.h"
#include "serial_protocol.h"

namespace {
    enum {
        ADDR_TYPE_BRIGHTNESS = 0x01
    };
    const int INTER_DEVICE_DELAY_USEC = 60000; // TBD: make it configurable
}

class TSerialContext: public TModbusContext
{
public:
    TSerialContext(PAbstractSerialPort port);
    void Connect();
    void Disconnect();
    void AddDevice(int slave, const std::string& protocol);
    void SetDebug(bool debug);
    void SetSlave(int slave);
    void ReadCoils(int addr, int nb, uint8_t *dest);
    void WriteCoil(int addr, int value);
    void ReadDisceteInputs(int addr, int nb, uint8_t *dest);
    void ReadHoldingRegisters(int addr, int nb, uint16_t *dest);
    void WriteHoldingRegisters(int addr, int nb, const uint16_t *data);
    void WriteHoldingRegister(int addr, uint16_t value);
    void ReadInputRegisters(int addr, int nb, uint16_t *dest);
    void ReadDirectRegister(int addr, uint64_t* dest, RegisterFormat format, size_t width);
    void WriteDirectRegister(int addr, uint64_t value, RegisterFormat format);
    void EndPollCycle(int usec);

private:
    PSerialProtocol GetProtocol();

    PAbstractSerialPort Port;
    int SlaveId;
    std::unordered_map<int, std::string> ProtoNameMap;
    std::unordered_map<int, PSerialProtocol> ProtoMap;
};

TSerialContext::TSerialContext(PAbstractSerialPort port):
    Port(port), SlaveId(-1) {}

void TSerialContext::Connect()
{
    try {
        if (!Port->IsOpen())
            Port->Open();
    } catch (const TSerialProtocolException& e) {
        throw TModbusException(e.what());
    }
}

void TSerialContext::Disconnect()
{
    if (Port->IsOpen())
        Port->Close();
}

void TSerialContext::AddDevice(int slave, const std::string& protocol)
{
    ProtoNameMap[slave] = protocol;
}

void TSerialContext::SetDebug(bool debug)
{
    Port->SetDebug(debug);
}

void TSerialContext::SetSlave(int slave)
{
    if (SlaveId == slave)
        return;

    SlaveId = slave;
    if (Port->IsOpen())
        Port->USleep(INTER_DEVICE_DELAY_USEC);
}

void TSerialContext::ReadCoils(int addr, int nb, uint8_t *dest)
{
    auto proto = GetProtocol();
    try {
        Connect();
        for (int i = 0; i < nb; ++i)
            *dest++ = proto->ReadRegister(SlaveId, addr + i, U8) == 0 ? 0 : 1;
    } catch (const TSerialProtocolTransientErrorException& e) {
        throw TModbusException(e.what());
    } catch (const TSerialProtocolException& e) {
        Disconnect();
        throw TModbusException(e.what());
    }
}

void TSerialContext::WriteCoil(int addr, int value)
{
    try {
        Connect();
        GetProtocol()->WriteRegister(SlaveId, addr, value ? 0xff : 0, U8);
    } catch (const TSerialProtocolTransientErrorException& e) {
        throw TModbusException(e.what());
    } catch (const TSerialProtocolException& e) {
        Disconnect();
        throw TModbusException(e.what());
    }
}

void TSerialContext::ReadDisceteInputs(int addr, int nb, uint8_t *dest)
{
    ReadCoils(addr, nb, dest);
}

void TSerialContext::ReadHoldingRegisters(int addr, int nb, uint16_t *dest)
{
    auto proto = GetProtocol();
    try {
        Connect();
        for (int i = 0; i < nb; ++i) {
            // so far, all Uniel address types store register to read
            // in the low byte
            *dest++ = proto->ReadRegister(SlaveId, (addr + i) & 0xFF, U16);
        }
    } catch (const TSerialProtocolTransientErrorException& e) {
        throw TModbusException(e.what());
    } catch (const TSerialProtocolException& e) {
        Disconnect();
        throw TModbusException(e.what());
    }
}

void TSerialContext::WriteHoldingRegister(int addr, uint16_t value)
{
    auto proto = GetProtocol();
    try {
        Connect();
		if ( (addr >= 0x00) && (addr <= 0xFF) ) {
			// address is between 0x00 and 0xFF, so treat it as
			// normal Uniel register (read via 0x05, write via 0x06)
			proto->WriteRegister(SlaveId, addr, value, U16);
		} else {
			int addr_type = addr >> 24;
			if (addr_type == ADDR_TYPE_BRIGHTNESS ) {
				// address is 0x01XXWWRR, where RR is register to read
				// via 0x05 cmd, WW - register to write via 0x0A cmd
				uint8_t addr_write = (addr & 0xFF00) >> 8;
				proto->SetBrightness(SlaveId, addr_write, value);
			} else {
				throw TModbusException("unsupported register address: " + std::to_string(addr));
			}
		}
    } catch (const TSerialProtocolTransientErrorException& e) {
        throw TModbusException(e.what());
    } catch (const TSerialProtocolException& e) {
        Disconnect();
        throw TModbusException(e.what());
    }
}

void TSerialContext::WriteHoldingRegisters(int addr, int nb, const uint16_t *data)
{
	for (int i = 0; i < nb; ++i) {
		WriteHoldingRegister(addr + i, data[i]);
	}
}

void TSerialContext::ReadInputRegisters(int addr, int nb, uint16_t *dest)
{
    ReadHoldingRegisters(addr, nb, dest);
}

void TSerialContext::ReadDirectRegister(int addr, uint64_t* dest, RegisterFormat format, size_t width) {
    try {
        Connect();
        *dest++ = GetProtocol()->ReadRegister(SlaveId, addr, format, width);
    } catch (const TSerialProtocolTransientErrorException& e) {
        throw TModbusException(e.what());
    } catch (const TSerialProtocolException& e) {
        Disconnect();
        throw TModbusException(e.what());
    }    
}

void TSerialContext::WriteDirectRegister(int addr, uint64_t value, RegisterFormat format) {
    try {
        Connect();
        GetProtocol()->WriteRegister(SlaveId, addr, value, format);
    } catch (const TSerialProtocolTransientErrorException& e) {
        throw TModbusException(e.what());
    } catch (const TSerialProtocolException& e) {
        Disconnect();
        throw TModbusException(e.what());
    }
}

void TSerialContext::EndPollCycle(int usecDelay)
{
    for (const auto& p: ProtoMap)
        p.second->EndPollCycle();
    usleep(usecDelay);
}

PSerialProtocol TSerialContext::GetProtocol()
{
    if (SlaveId < 0)
        throw TModbusException("slave id not set");

    auto it = ProtoMap.find(SlaveId);
    if (it != ProtoMap.end())
        return it->second;
    auto nameIt = ProtoNameMap.find(SlaveId);
    if (nameIt == ProtoNameMap.end())
        throw TModbusException("slave not found");

    try {
        auto protocol = TSerialProtocolFactory::CreateProtocol(nameIt->second, Port);
        return ProtoMap[SlaveId] = protocol;
    } catch (const TSerialProtocolException& e) {
        Disconnect();
        throw TModbusException(e.what());
    }
}

PModbusContext TSerialConnector::CreateContext(PAbstractSerialPort port)
{
    return std::make_shared<TSerialContext>(port);
}

PModbusContext TSerialConnector::CreateContext(const TSerialPortSettings& settings)
{
    return CreateContext(std::make_shared<TSerialPort>(settings));
}

// TBD: support debug mode
// TBD: brightness values via direct regs
// TBD: serial context for 'uniel' -- backward compat -- pass it down as
// default device type
