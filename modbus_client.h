#ifndef _MODBUS_CLIENT_H_
#define _MODBUS_CLIENT_H_

#include <map>
#include <string>
#include <memory>
#include <sstream>
#include <exception>
#include <functional>

#include <modbus/modbus.h>

class TModbusHandler;

struct TModbusParameter
{
    enum Type { COIL, DISCRETE_INPUT, HOLDING_REGITER, INPUT_REGISTER };
    TModbusParameter(int _slave = 0, Type _type = COIL, int _address = 0)
        : slave(_slave), type(_type), address(_address) {}
    int slave;
    Type type;
    int address;

    std::string str() const {
        std::stringstream s;
        s << "<" << slave << ":" <<
            (type == COIL ? "coil" :
             type == DISCRETE_INPUT ? "discrete" :
             type == HOLDING_REGITER ? "holding" :
             type == INPUT_REGISTER ? "input" :
             "bad") << ": " << address << ">";
        return s.str();
    }

    bool operator< (const TModbusParameter& param) const {
        if (type < param.type)
            return true;
        if (type > param.type)
            return false;
        return address < param.address;
    }

    bool operator== (const TModbusParameter& param) const {
        return type == param.type && address == param.address;
    }
};

namespace std {
    template <> struct hash<TModbusParameter> {
        size_t operator()(const TModbusParameter& p) const {
            return hash<int>()(p.type) ^ hash<int>()(p.address);
        }
    };
}

class TModbusException: public std::exception {
public:
    TModbusException(std::string _message): message(_message) {}
    const char* what () const throw ()
    {
        return ("Modbus error: " + message).c_str();
    }
private:
    std::string message;
};

typedef std::function<void(const TModbusParameter& param, int value)> TModbusCallback;

class TModbusClient
{
public:
    TModbusClient(std::string _device, int _baud_rate, char _parity, int _data_bits, int _stop_bits);
    TModbusClient(const TModbusClient& client) = delete;
    TModbusClient& operator=(const TModbusClient&) = delete;
    ~TModbusClient();
    void AddParam(const TModbusParameter& param);
    void Connect();
    void Disconnect();
    void Loop();
    void SetValue(const TModbusParameter& param, int value);
    void SetCallback(const TModbusCallback& _callback);
    void SetPollInterval(int ms);
    void SetModbusDebug(bool debug);

private:
    TModbusHandler* CreateParameterHandler(const TModbusParameter& param);
    std::map< TModbusParameter, std::unique_ptr<TModbusHandler> > handlers;
    modbus_t* ctx;
    bool active;
    int poll_interval = 2000;
    const int MAX_REGS = 65536;
    TModbusCallback callback;
};

#endif /* _MODBUS_CLIENT_H_ */
