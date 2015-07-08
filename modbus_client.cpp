#include <iostream>
#include <cmath>
#include <mutex>
#include <unistd.h>
#include <vector>
#include <modbus/modbus.h>
#include "modbus_client.h"
#include <utility>
#include <sstream>

TModbusConnector::~TModbusConnector() {}

TModbusContext::~TModbusContext() {}

class TDefaultModbusContext: public TModbusContext {
public:
    TDefaultModbusContext(const TModbusConnectionSettings& settings);
    void Connect();
    void Disconnect();
    void SetDebug(bool debug);
    void SetSlave(int slave);
    void ReadCoils(int addr, int nb, uint8_t *dest);
    void WriteCoil(int addr, int value);
    void ReadDisceteInputs(int addr, int nb, uint8_t *dest);
    void ReadHoldingRegisters(int addr, int nb, uint16_t *dest);
    void WriteHoldingRegisters(int addr, int nb, const uint16_t *data);
    void ReadInputRegisters(int addr, int nb, uint16_t *dest);
    void USleep(int usec);
private:
    modbus_t* InnerContext;
};

TDefaultModbusContext::TDefaultModbusContext(const TModbusConnectionSettings& settings)
{
    InnerContext = modbus_new_rtu(settings.Device.c_str(), settings.BaudRate,
                                  settings.Parity, settings.DataBits, settings.StopBits);
    if (!InnerContext)
        throw TModbusException("failed to create modbus context");
    modbus_set_error_recovery(InnerContext, MODBUS_ERROR_RECOVERY_PROTOCOL); // FIXME

    if (settings.ResponseTimeoutMs > 0) {
        struct timeval tv;
        tv.tv_sec = settings.ResponseTimeoutMs / 1000;
        tv.tv_usec = (settings.ResponseTimeoutMs % 1000) * 1000;
        modbus_set_response_timeout(InnerContext, &tv);
    }
}

void TDefaultModbusContext::Connect()
{
    if (modbus_connect(InnerContext) != 0)
        throw TModbusException("couldn't initialize modbus connection");
    modbus_flush(InnerContext);
}

void TDefaultModbusContext::Disconnect()
{
    modbus_close(InnerContext);
}

void TDefaultModbusContext::SetDebug(bool debug)
{
    modbus_set_debug(InnerContext, debug);
}

void TDefaultModbusContext::SetSlave(int slave)
{
    modbus_set_slave(InnerContext, slave);
}

void TDefaultModbusContext::ReadCoils(int addr, int nb, uint8_t *dest)
{
    if (modbus_read_bits(InnerContext, addr, nb, dest) < nb)
        throw TModbusException("failed to read " + std::to_string(nb) +
                               " coil(s) @ " + std::to_string(addr));
}

void TDefaultModbusContext::WriteCoil(int addr, int value)
{
    if (modbus_write_bit(InnerContext, addr, value) < 0)
        throw TModbusException("failed to write coil @ " + std::to_string(addr));
}

void TDefaultModbusContext::ReadDisceteInputs(int addr, int nb, uint8_t *dest)
{
    if (modbus_read_input_bits(InnerContext, addr, nb, dest) < nb)
        throw TModbusException("failed to read " + std::to_string(nb) +
                               "discrete input(s) @ " + std::to_string(addr));
}

void TDefaultModbusContext::ReadHoldingRegisters(int addr, int nb, uint16_t *dest)
{
    if (modbus_read_registers(InnerContext, addr, nb, dest) < nb)
        throw TModbusException("failed to read " + std::to_string(nb) +
                               " holding register(s) @ " + std::to_string(addr));
}

void TDefaultModbusContext::WriteHoldingRegisters(int addr, int nb, const uint16_t *data)
{
    if (modbus_write_registers(InnerContext, addr, nb, data) < nb)
        throw TModbusException("failed to write " + std::to_string(nb) +
                               " holding register(s) @ " + std::to_string(addr));
}

void TDefaultModbusContext::ReadInputRegisters(int addr, int nb, uint16_t *dest)
{
    if (modbus_read_input_registers(InnerContext, addr, nb, dest) < nb)
        throw TModbusException("failed to read " + std::to_string(nb) +
                               " input register(s) @ " + std::to_string(addr));
}

void TDefaultModbusContext::USleep(int usec)
{
    usleep(usec);
}

PModbusContext TDefaultModbusConnector::CreateContext(const TModbusConnectionSettings& settings)
{
    return PModbusContext(new TDefaultModbusContext(settings));
}

typedef std::pair<bool, int> TErrorMessage;

class TRegisterHandler
{
public:
    TRegisterHandler(const TModbusClient* client, std::shared_ptr<TModbusRegister> _reg)
        : Client(client), reg(_reg) {}
    virtual ~TRegisterHandler() {}
    virtual std::vector<uint16_t> Read(PModbusContext ctx) = 0;
    virtual void Write(PModbusContext ctx, const std::vector<uint16_t> & v);
    std::shared_ptr<TModbusRegister> Register() const { return reg; }
    TErrorMessage Poll(PModbusContext ctx);
    int Flush(PModbusContext ctx);
    std::string TextValue() const;

    void SetTextValue(const std::string& v);
    bool DidRead() const { return did_read; }
protected:
    const TModbusClient* Client;

	template<typename T>
	std::string ToScaledTextValue(T val) const;

	template<typename T>
	T FromScaledTextValue(const std::string& str) const;

	std::vector<uint16_t> ConvertMasterValue(const std::string& v) const;

private:
    std::vector<uint16_t> value;
    std::shared_ptr<TModbusRegister> reg;
    volatile bool dirty = false;
    bool did_read = false;
    std::mutex set_value_mutex;
};

void TRegisterHandler::Write(PModbusContext, const std::vector<uint16_t> &)
{
    throw TModbusException("trying to write read-only register");
};

TErrorMessage TRegisterHandler::Poll(PModbusContext ctx)
{
    int message = 0;
    // set poll error message empty
    if (reg->ErrorMessage == "Poll") {
        reg->ErrorMessage = "";
        message = 2; // we need to delete error message
    }
    if (!reg->Poll || dirty)
        return std::make_pair(false, 0); // write-only register

    bool first_poll = !did_read;
    std::vector<uint16_t> new_value;
    ctx->SetSlave(reg->Slave);
    try {
        new_value = Read(ctx);
    } catch (const TModbusException& e) {
        std::cerr << "TRegisterHandler::Poll(): warning: " << e.what() << " slave_id is "
				  << 	reg->Slave << "(0x" << std::hex << reg->Slave << ")" << std::endl;
        std::cerr << std::dec;
        reg->ErrorMessage = "Poll";
        return std::make_pair(true, 1);
    }
    did_read = true;
    set_value_mutex.lock();
    if (value != new_value) {
        if (dirty) {
            set_value_mutex.unlock();
            return std::make_pair(true, message);
        }
        value = new_value;
        set_value_mutex.unlock();

        if (Client->DebugEnabled()) {
            std::cerr << "new val for " << reg->ToString() << ": " ;
            for (const auto & el : new_value) {
				std::cerr << el << " ";
			}
			std::cerr << std::endl;
		}
        return std::make_pair(true, message);
    } else
        set_value_mutex.unlock();
    return std::make_pair(first_poll, message);
}

int TRegisterHandler::Flush(PModbusContext ctx)
{
    int message = 0;
    // set flush error message empty
    if (reg->ErrorMessage == "Flush") {
        reg->ErrorMessage = "";
        message = 2;
    }
    set_value_mutex.lock();
    if (dirty) {
        dirty = false;
        set_value_mutex.unlock();
        ctx->SetSlave(reg->Slave);
        try {
            Write(ctx, value);
        } catch (const TModbusException& e) {
            std::cerr << "TRegisterHandler::Flush(): warning: " << e.what() << " slave_id is " << reg->Slave << "(0x" << std::hex << reg->Slave << ")" <<  std::endl;
            std::cerr << std::dec;
            reg->ErrorMessage = "Flush";
            return 1;
        }
    }
    else {
        set_value_mutex.unlock();
    }
    return message;
}

std::string TRegisterHandler::TextValue() const
{
    switch (reg->Format) {
    case TModbusRegister::U16:
        return ToScaledTextValue(value[0]);
    case TModbusRegister::S16:
        return ToScaledTextValue((int16_t)value[0]);
    case TModbusRegister::U8:
        return ToScaledTextValue(value[0] & 255);
    case TModbusRegister::S8:
        return ToScaledTextValue((int8_t) value[0]);
    case TModbusRegister::S32:
        return ToScaledTextValue(
			static_cast<int32_t>((static_cast<uint32_t>(value[0]) << 16) | static_cast<uint32_t>(value[1]))
		);
    case TModbusRegister::U32:
        return ToScaledTextValue((static_cast<uint32_t>(value[0]) << 16) | static_cast<uint32_t>(value[1]));
    case TModbusRegister::S64:
        return ToScaledTextValue(static_cast<int64_t> (
							         (  static_cast<uint64_t>(value[0]) << 48) | \
					                 ( static_cast<uint64_t>(value[1]) << 32) | \
					                 ( static_cast<uint64_t>(value[2]) << 16) | \
					                   static_cast<uint64_t>(value[3])
				                 ));
	case TModbusRegister::Float:
		{
		uint32_t tmp = (static_cast<uint32_t>(value[0]) << 16) | static_cast<uint32_t>(value[1]);
		return ToScaledTextValue(*( (float*) (&tmp)));
		}

    case TModbusRegister::Double:
		{
		uint64_t tmp2 = (  static_cast<uint64_t>(value[0]) << 48) | \
	                   ( static_cast<uint64_t>(value[1]) << 32) | \
	                   ( static_cast<uint64_t>(value[2]) << 16) | \
	                     static_cast<uint64_t>(value[3]);
		return ToScaledTextValue(*((double*)(&tmp2)));
		}
    default:
        return ToScaledTextValue(value[0]);
    }

}

template<typename A>
std::string TRegisterHandler::ToScaledTextValue(A val) const
{
	if (reg->Scale == 1) {
		return std::to_string(val);
	} else {
		return std::to_string(reg->Scale * val);
	}
}



template<>
uint64_t TRegisterHandler::FromScaledTextValue(const std::string& str) const
{
    if (reg->Scale == 1) {
		return std::stoull(str);
	 } else {
		 return round(stod(str) / reg->Scale);
	}
}


template<>
int64_t TRegisterHandler::FromScaledTextValue(const std::string& str) const
{
    if (reg->Scale == 1) {
		return std::stoll(str);
	 } else {
		 return round(stod(str) / reg->Scale);
	}
}


template<>
double TRegisterHandler::FromScaledTextValue(const std::string& str) const
{
	 return stod(str) / reg->Scale;
}



void TRegisterHandler::SetTextValue(const std::string& v)
{
    std::lock_guard<std::mutex> lock(set_value_mutex);
    dirty = true;
    value = ConvertMasterValue(v);
}


std::vector<uint16_t> TRegisterHandler::ConvertMasterValue(const std::string& str) const
{
    switch (reg->Format) {
    case TModbusRegister::S16:
        return {FromScaledTextValue<int64_t>(str) & 65535};
    case TModbusRegister::U8:
    case TModbusRegister::S8:
        return {FromScaledTextValue<int64_t>(str) & 255};
    case TModbusRegister::S32:
    case TModbusRegister::U32:
	    {
			auto v = FromScaledTextValue<int64_t>(str);
	        return { (v >> 16) & 0xFFFF, v & 0xFFFF};
		}
    case TModbusRegister::S64:
		{
			auto v = FromScaledTextValue<int64_t>(str);
	        return { v >> 48, (v >> 32) & 0xFFFF, (v >> 16) & 0xFFFF, v & 0xFFFF};
		}

	case TModbusRegister::Float:
		{
			float tmp = FromScaledTextValue<double>(str);
			uint32_t v = *((uint32_t *) (&tmp));
	        return { (v >> 16) & 0xFFFF, v & 0xFFFF};
		}

	case TModbusRegister::Double:
		{
			double tmp = FromScaledTextValue<double>(str);
			uint64_t v = *((uint64_t *) (&tmp));
	        return { v >> 48, (v >> 32) & 0xFFFF, (v >> 16) & 0xFFFF, v & 0xFFFF};
		}

    case TModbusRegister::U16:
    default:
        return {FromScaledTextValue<int64_t>(str)};
    }
}
class TCoilHandler: public TRegisterHandler
{
public:
    TCoilHandler(const TModbusClient* client, std::shared_ptr<TModbusRegister> _reg)
        : TRegisterHandler(client, _reg) {}

    std::vector<uint16_t> Read(PModbusContext ctx) {
        unsigned char b;
        ctx->ReadCoils(Register()->Address, 1, &b);
        return {static_cast<uint16_t>(b & 1)};
    }

    void Write(PModbusContext ctx, const std::vector<uint16_t> & v) {
        ctx->WriteCoil(Register()->Address, v[0]);
    }
};

class TDiscreteInputHandler: public TRegisterHandler
{
public:
    TDiscreteInputHandler(const TModbusClient* client, std::shared_ptr<TModbusRegister> _reg)
        : TRegisterHandler(client, _reg) {}

    std::vector<uint16_t> Read(PModbusContext ctx) {
        uint8_t b;
        ctx->ReadDisceteInputs(Register()->Address, 1, &b);
        return {static_cast<uint16_t>(b & 1)};
    }
};

class THoldingRegisterHandler: public TRegisterHandler
{
public:
    THoldingRegisterHandler(const TModbusClient* client, std::shared_ptr<TModbusRegister> _reg)
        : TRegisterHandler(client, _reg) {}

    std::vector<uint16_t> Read(PModbusContext ctx) {
        std::vector<uint16_t> v;
        v.resize(Register()->Width());
        ctx->ReadHoldingRegisters(Register()->Address, Register()->Width(), &v[0]);
        return v;
    }

    void Write(PModbusContext ctx, const std::vector<uint16_t> & v) {
        // FIXME: use
        if (Client->DebugEnabled())
            std::cerr << "write: " << Register()->ToString() << std::endl;
        ctx->WriteHoldingRegisters(Register()->Address, Register()->Width(), &v[0]);
    }
};

class TInputRegisterHandler: public TRegisterHandler
{
public:
    TInputRegisterHandler(const TModbusClient* client, std::shared_ptr<TModbusRegister> _reg)
        : TRegisterHandler(client, _reg) {}

    std::vector<uint16_t> Read(PModbusContext ctx) {
        std::vector<uint16_t> v;
        v.resize(Register()->Width());
        ctx->ReadInputRegisters(Register()->Address, Register()->Width(), &v[0]);
        return v;
    }
};

TModbusClient::TModbusClient(const TModbusConnectionSettings& settings,
                             PModbusConnector connector)
    : Active(false),
      PollInterval(1000)
{
    if (!connector)
        connector = PModbusConnector(new TDefaultModbusConnector);
    Context = connector->CreateContext(settings);
}

TModbusClient::~TModbusClient()
{
    if (Active)
        Disconnect();
}

void TModbusClient::AddRegister(std::shared_ptr<TModbusRegister> reg)
{
    if (Active)
        throw TModbusException("can't add registers to the active client");
    if (handlers.find(reg) != handlers.end())
        throw TModbusException("duplicate register");
    handlers[reg] = std::unique_ptr<TRegisterHandler>(CreateRegisterHandler(reg));
}

void TModbusClient::Connect()
{
    if (Active)
        return;
    if (!handlers.size())
        throw TModbusException("no registers defined");
    Context->Connect();
    Active = true;
}

void TModbusClient::Disconnect()
{
    Context->Disconnect();
    Active = false;
}

void TModbusClient::Cycle()
{
    Connect();

    // FIXME: that's suboptimal polling implementation.
    // Need to implement bunching of Modbus registers.
    // Note that for multi-register values, all values
    // corresponding to single register should be retrieved
    // by single query.
    for (const auto& p: handlers) {
        for(const auto& q: handlers) {
            int flush_message = q.second->Flush(Context);
            if ((flush_message == 1) && (ErrorCallback)) {
                ErrorCallback(q.first);
            }
            if ((flush_message == 2) && (DeleteErrorsCallback)) {
                DeleteErrorsCallback(q.first);
            }
        }

        const auto& poll_message = p.second->Poll(Context);
        if ((poll_message.second == 1) && (ErrorCallback)) {
            ErrorCallback(p.first);
        }
        if ((poll_message.second == 2) && (DeleteErrorsCallback)) {
                DeleteErrorsCallback(p.first);
        }
        if ((poll_message.first) && (Callback) && (poll_message.second != 1)) {
                Callback(p.first);
            }
        Context->USleep(PollInterval * 1000);
    }
}

void TModbusClient::WriteHoldingRegister(int slave, int address, uint16_t value)
{
    Connect();
    Context->SetSlave(slave);
    Context->WriteHoldingRegisters(address, 1, &value);
}



void TModbusClient::SetTextValue(std::shared_ptr<TModbusRegister> reg, const std::string& value)
{
    GetHandler(reg)->SetTextValue(value);
}

std::string TModbusClient::GetTextValue(std::shared_ptr<TModbusRegister> reg) const
{
    return GetHandler(reg)->TextValue();
}

bool TModbusClient::DidRead(std::shared_ptr<TModbusRegister> reg) const
{
    return GetHandler(reg)->DidRead();
}

void TModbusClient::SetCallback(const TModbusCallback& callback)
{
    Callback = callback;
}

void TModbusClient::SetErrorCallback(const TModbusCallback& callback)
{
    ErrorCallback = callback;
}

void TModbusClient::SetDeleteErrorsCallback(const TModbusCallback& callback)
{
    DeleteErrorsCallback = callback;
}

void TModbusClient::SetPollInterval(int interval)
{
    PollInterval = interval;
}

void TModbusClient::SetModbusDebug(bool debug)
{
    Context->SetDebug(debug);
    Debug = debug;
}

bool TModbusClient::DebugEnabled() const {
    return Debug;
}

const std::unique_ptr<TRegisterHandler>& TModbusClient::GetHandler(std::shared_ptr<TModbusRegister> reg) const
{
    auto it = handlers.find(reg);
    if (it == handlers.end())
        throw TModbusException("register not found");
    return it->second;
}

TRegisterHandler* TModbusClient::CreateRegisterHandler(std::shared_ptr<TModbusRegister> reg)
{
    switch (reg->Type) {
    case TModbusRegister::RegisterType::COIL:
        return new TCoilHandler(this, reg);
    case TModbusRegister::RegisterType::DISCRETE_INPUT:
        return new TDiscreteInputHandler(this, reg);
    case TModbusRegister::RegisterType::HOLDING_REGISTER:
        return new THoldingRegisterHandler(this, reg);
    case TModbusRegister::RegisterType::INPUT_REGISTER:
        return new TInputRegisterHandler(this, reg);
    default:
        throw TModbusException("bad register type");
    }
}
