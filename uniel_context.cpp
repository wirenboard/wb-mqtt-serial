#include <unistd.h>

#include "uniel_context.h"

TUnielModbusContext::TUnielModbusContext(const std::string& device, int timeout_ms):
    Bus(device, timeout_ms), SlaveAddr(1) {}

void TUnielModbusContext::Connect()
{
    try {
        if (!Bus.IsOpen())
            Bus.Open();
    } catch (const TSerialProtocolException& e) {
        throw TModbusException(e.what());
    }
}

void TUnielModbusContext::Disconnect()
{
    if (Bus.IsOpen())
        Bus.Close();
}

void TUnielModbusContext::SetDebug(bool)
{
    // TBD: set debug
}

void TUnielModbusContext::SetSlave(int slave_addr)
{
    SlaveAddr = slave_addr;
}

void TUnielModbusContext::ReadCoils(int addr, int nb, uint8_t *dest)
{
    try {
        Connect();
        for (int i = 0; i < nb; ++i)
            *dest++ = Bus.ReadRegister(SlaveAddr, addr + i) == 0 ? 0 : 1;
    } catch (const TSerialProtocolTransientErrorException& e) {
        throw TModbusException(e.what());
    } catch (const TSerialProtocolException& e) {
        Disconnect();
        throw TModbusException(e.what());
    }
}

void TUnielModbusContext::WriteCoil(int addr, int value)
{
    try {
        Connect();
        Bus.WriteRegister(SlaveAddr, addr, value ? 0xff : 0);
    } catch (const TSerialProtocolTransientErrorException& e) {
        throw TModbusException(e.what());
    } catch (const TSerialProtocolException& e) {
        Disconnect();
        throw TModbusException(e.what());
    }
}

void TUnielModbusContext::ReadDisceteInputs(int addr, int nb, uint8_t *dest)
{
    ReadCoils(addr, nb, dest);
}

void TUnielModbusContext::ReadHoldingRegisters(int addr, int nb, uint16_t *dest)
{
    try {
        Connect();
        for (int i = 0; i < nb; ++i) {
            // so far, all Uniel address types store register to read
            // in the low byte
            *dest++ = Bus.ReadRegister(SlaveAddr, (addr + i) & 0xFF );
        }
    } catch (const TSerialProtocolTransientErrorException& e) {
        throw TModbusException(e.what());
    } catch (const TSerialProtocolException& e) {
        Disconnect();
        throw TModbusException(e.what());
    }
}

void TUnielModbusContext::WriteHoldingRegister(int addr, uint16_t value)
{
    try {
        Connect();
		if ( (addr >= 0x00) && (addr <= 0xFF) ) {
			// address is between 0x00 and 0xFF, so treat it as
			// normal Uniel register (read via 0x05, write via 0x06)
			Bus.WriteRegister(SlaveAddr, addr, value);
		} else {
			int addr_type = addr >> 24;
			if (addr_type == ADDR_TYPE_BRIGHTNESS ) {
				// address is 0x01XXWWRR, where RR is register to read
				// via 0x05 cmd, WW - register to write via 0x0A cmd
				uint8_t addr_write = (addr & 0xFF00) >> 8;
				Bus.SetBrightness(SlaveAddr, addr_write, value);
			} else {
				throw TModbusException("unsupported Uniel register address: " + std::to_string(addr));
			}
		}
    } catch (const TSerialProtocolTransientErrorException& e) {
        throw TModbusException(e.what());
    } catch (const TSerialProtocolException& e) {
        Disconnect();
        throw TModbusException(e.what());
    }
}

void TUnielModbusContext::WriteHoldingRegisters(int addr, int nb, const uint16_t *data)
{
	for (int i = 0; i < nb; ++i) {
		WriteHoldingRegister(addr + i, data[i]);
	}
}

void TUnielModbusContext::ReadInputRegisters(int addr, int nb, uint16_t *dest)
{
    ReadHoldingRegisters(addr, nb, dest);
}

void TUnielModbusContext::USleep(int usec)
{
    usleep(usec);
}

PModbusContext TUnielModbusConnector::CreateContext(const TModbusConnectionSettings& settings)
{
    int timeout = settings.ResponseTimeoutMs ? settings.ResponseTimeoutMs : TUnielBus::DefaultTimeoutMs;
    return PModbusContext(new TUnielModbusContext(settings.Device, timeout));
}

// TBD: debug
// TBD: brightness values (0x100 + channel as holding register index)
// TBD: port settings (do we need them?)
