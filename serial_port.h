#pragma once
#include <chrono>
#include <termios.h>

#include "file_descriptor_port.h"
#include "serial_port_settings.h"


class TSerialPort final: public TFileDescriptorPort
{
    using Base = TFileDescriptorPort;
public:
    TSerialPort(const PSerialPortSettings & settings);
    ~TSerialPort() = default;

    void Open() override;
    void Close() override;

private:
    PSerialPortSettings Settings;
    termios             OldTermios;
};

using PSerialPort = std::shared_ptr<TSerialPort>;
