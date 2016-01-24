#include <map>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <fcntl.h>
#include <stdio.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <iostream>
#include <iomanip>
#include <utility>

#include "serial_protocol.h"

TLibModbusContext::TLibModbusContext(const TSerialPortSettings& settings)
    : Inner(modbus_new_rtu(settings.Device.c_str(), settings.BaudRate,
                           settings.Parity, settings.DataBits, settings.StopBits)) {}

TAbstractSerialPort::~TAbstractSerialPort() {}

TSerialPort::TSerialPort(const TSerialPortSettings& settings)
    : Settings(settings),
      Context(new TLibModbusContext(settings)),
      Debug(false),
      Fd(-1) {}

TSerialPort::~TSerialPort()
{
    if (Fd >= 0)
        modbus_close(Context->Inner);
}

void TSerialPort::SetDebug(bool debug)
{
    Debug = debug;
}

void TSerialPort::Open()
{
    if (Fd >= 0)
        throw TSerialProtocolException("port already open");
    if (modbus_connect(Context->Inner) < 0)
        throw TSerialProtocolException("cannot open serial port");
    Fd = modbus_get_socket(Context->Inner);
}

void TSerialPort::Close()
{
    CheckPortOpen();
    modbus_close(Context->Inner);
    Fd = -1;
}

bool TSerialPort::IsOpen() const
{
    return Fd >= 0;
}

void TSerialPort::CheckPortOpen()
{
    if (Fd < 0)
        throw TSerialProtocolException("port not open");
}

void TSerialPort::WriteBytes(const uint8_t* buf, int count) {
    if (write(Fd, buf, count) < count)
        throw TSerialProtocolException("serial write failed");
    if (Debug) {
        // TBD: move this to libwbmqtt (HexDump?)
        std::ios::fmtflags f(std::cerr.flags());
        std::cerr << "Write:" << std::hex << std::setfill('0');
        for (int i = 0; i < count; ++i)
            std::cerr << " " << std::setw(2) << int(buf[i]);
        std::cerr << std::endl;
        std::cerr.flags(f);
    }
}

bool TSerialPort::Select(int ms)
{
    fd_set rfds;
    struct timeval tv, *tvp = 0;

    #if 0
    // too verbose
    if (Debug)
        std::cerr << "Select on " << Settings.Device << ": " << ms << " ms" << std::endl;
    #endif

    FD_ZERO(&rfds);
    FD_SET(Fd, &rfds);
    if (ms > 0) {
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        tvp = &tv;
    }

    int r = select(Fd + 1, &rfds, NULL, NULL, tvp);
    if (r < 0)
        throw TSerialProtocolException("select() failed");

    return r > 0;
}

uint8_t TSerialPort::ReadByte()
{
    CheckPortOpen();

    if (!Select(Settings.ResponseTimeoutMs))
        throw TSerialProtocolTransientErrorException("timeout");

    uint8_t b;
    if (read(Fd, &b, 1) < 1)
        throw TSerialProtocolException("read() failed");

    if (Debug) {
        std::ios::fmtflags f(std::cerr.flags());
        std::cerr << "Read: " << std::hex << std::setw(2) << std::setfill('0') << int(b) << std::endl;
        std::cerr.flags(f);
    }

    return b;
}

int TSerialPort::ReadFrame(uint8_t* buf, int size, int timeout)
{
    CheckPortOpen();
    int nread = 0;
    while (nread < size) {
        if (!Select(!nread ? Settings.ResponseTimeoutMs : timeout))
            break; // end of the frame

        // We don't want to use non-blocking IO in general
        // (e.g. we want blocking writes), but we don't want
        // read() call below to block because actual frame
        // size is not known at this point. So we must
        // know how many bytes are available
        int nb;
        if (ioctl(Fd, FIONREAD, &nb) < 0)
            throw TSerialProtocolException("FIONREAD ioctl() failed");
        if (!nb)
            continue; // shouldn't happen, actually
        if (nb > size - nread)
            nb = size - nread;

        int n = read(Fd, buf + nread, nb);
        if (n < 0)
            throw TSerialProtocolException("read() failed");
        if (n < nb) // may happen only due to a kernel/driver bug
            throw TSerialProtocolException("short read()");

        nread += nb;
    }

    if (!nread)
        throw TSerialProtocolException("request timed out");

    if (Debug) {
        // TBD: move this to libwbmqtt (HexDump?)
        std::ios::fmtflags f(std::cerr.flags());
        std::cerr << "ReadFrame:" << std::hex << std::uppercase << std::setfill('0');
        for (int i = 0; i < nread; ++i) {
            std::cerr << " " << std::setw(2) << int(buf[i]);
        }
        std::cerr << std::endl;
        std::cerr.flags(f);
    }

    return nread;
}

void TSerialPort::SkipNoise()
{
    uint8_t b;
    while (Select(NoiseTimeoutMs)) {
        if (read(Fd, &b, 1) < 1)
            throw TSerialProtocolException("read() failed");
        if (Debug) {
            std::ios::fmtflags f(std::cerr.flags());
            std::cerr << "read noise: " << std::hex << std::setfill('0') << std::setw(2) << int(b) << std::endl;
            std::cerr.flags(f);
        }

    }
}

void TSerialPort::USleep(int usec)
{
    usleep(usec);
}

PLibModbusContext TSerialPort::LibModbusContext() const
{
    return Context;
}

TSerialProtocol::TSerialProtocol(PAbstractSerialPort port)
    : SerialPort(port) {}

TSerialProtocol::~TSerialProtocol() {}

void TSerialProtocol::EndPollCycle() {}

std::unordered_map<std::string, TSerialProtocolEntry>
    *TSerialProtocolFactory::Protocols = 0;

void TSerialProtocolFactory::RegisterProtocol(const std::string& name, TSerialProtocolMaker maker,
                                              const TRegisterTypes& register_types)
{
    if (!Protocols)
        Protocols = new std::unordered_map<std::string, TSerialProtocolEntry>();

    auto reg_map = std::make_shared<TRegisterTypeMap>();
    for (const auto& rt : register_types)
        reg_map->emplace(rt.Name, rt);

    Protocols->emplace(name, TSerialProtocolEntry(maker, reg_map));
}

const TSerialProtocolEntry& TSerialProtocolFactory::GetProtocolEntry(PDeviceConfig device_config)
{
    auto it = Protocols->find(device_config->Protocol);
    if (it == Protocols->end())
        throw TSerialProtocolException("unknown serial protocol");
    return it->second;
}

PSerialProtocol TSerialProtocolFactory::CreateProtocol(PDeviceConfig device_config, PAbstractSerialPort port)
{
    return GetProtocolEntry(device_config).Maker(device_config, port);
}

PRegisterTypeMap TSerialProtocolFactory::GetRegisterTypes(PDeviceConfig device_config)
{
    return GetProtocolEntry(device_config).RegisterTypes;
}
