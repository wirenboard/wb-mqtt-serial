#include "serial_port.h"
#include "iec_common.h"
#include "log.h"
#include "serial_exc.h"

#include <cmath>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string.h>
#include <unistd.h>
#include <utility>

#include <linux/serial.h>
#include <sys/ioctl.h>

#define LOG(logger) ::logger.Log() << "[serial port] "

using namespace WBMQTT;

namespace
{
    int ConvertBaudRate(int rate)
    {
        switch (rate) {
            case 110:
                return B110;
            case 300:
                return B300;
            case 600:
                return B600;
            case 1200:
                return B1200;
            case 2400:
                return B2400;
            case 4800:
                return B4800;
            case 9600:
                return B9600;
            case 19200:
                return B19200;
            case 38400:
                return B38400;
            case 57600:
                return B57600;
            case 115200:
                return B115200;
            default:
                LOG(Warn) << "unsupported baud rate " << rate << " defaulting to 9600";
                return B9600;
        }
    }

    int ConvertDataBits(int data_bits)
    {
        switch (data_bits) {
            case 5:
                return CS5;
            case 6:
                return CS6;
            case 7:
                return CS7;
            case 8:
                return CS8;
            default:
                LOG(Warn) << "unsupported data bits count " << data_bits << " defaulting to 8";
                return CS8;
        }
    }

    std::chrono::milliseconds GetLinuxLag(int baudRate)
    {
        return std::chrono::milliseconds(baudRate < 9600 ? 34 : 24);
    }

    size_t GetRxTrigBytes(const std::string& path)
    {
        std::filesystem::path dev(path);
        while (std::filesystem::is_symlink(dev)) {
            dev = std::filesystem::read_symlink(dev);
        }
        auto rxTrigBytesPath = "/sys/class/tty" / dev.filename() / "rx_trig_bytes";
        std::ifstream f(rxTrigBytesPath);
        if (f.is_open()) {
            size_t val;
            try {
                f >> val;
                if (f.good()) {
                    LOG(Info) << rxTrigBytesPath << " = " << val;
                    return val;
                }
            } catch (const std::exception& e) {
                LOG(Warn) << rxTrigBytesPath << " read failed: " << e.what();
            }
        }
        return 0;
    }

    void MakeTermios(const TSerialPortSettings& settings, termios& dev)
    {
        memset(&dev, 0, sizeof(termios));
        auto baud_rate = ConvertBaudRate(settings.BaudRate);
        if (cfsetospeed(&dev, baud_rate) != 0 || cfsetispeed(&dev, baud_rate) != 0) {
            throw std::runtime_error("can't set baud rate " + std::to_string(settings.BaudRate) + " " +
                                    FormatErrno(errno));
        }

        if (settings.StopBits == 1) {
            dev.c_cflag &= ~CSTOPB;
        } else {
            dev.c_cflag |= CSTOPB;
        }

        switch (settings.Parity) {
            case 'N':
                dev.c_cflag &= ~PARENB;
                dev.c_iflag &= ~INPCK;
                break;
            case 'E':
                dev.c_cflag |= PARENB;
                dev.c_cflag &= ~PARODD;
                dev.c_iflag |= INPCK;
                break;
            case 'O':
                dev.c_cflag |= PARENB;
                dev.c_cflag |= PARODD;
                dev.c_iflag |= INPCK;
                break;
            default:
                std::stringstream ss;
                ss << "invalid parity value: ";
                if (isprint(settings.Parity)) {
                    ss << "'" << settings.Parity << "'";
                } else {
                    ss << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(2)
                    << int(settings.Parity);
                }
                throw std::runtime_error(ss.str());
        }

        dev.c_cflag = (dev.c_cflag & ~CSIZE) | ConvertDataBits(settings.DataBits) | CREAD | CLOCAL;
        dev.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        dev.c_iflag &= ~(IXON | IXOFF | IXANY);
        dev.c_oflag &= ~OPOST;
        dev.c_cc[VMIN] = 0;
        dev.c_cc[VTIME] = 0;
    }
}

TSerialPort::TSerialPort(const TSerialPortSettings& settings)
    : Settings(settings),
      RxTrigBytes(GetRxTrigBytes(Settings.Device))
{
    memset(&OldTermios, 0, sizeof(termios));
}

void TSerialPort::Open()
{
    try {
        if (IsOpen())
            throw std::runtime_error("port is already open");

        Fd = open(Settings.Device.c_str(), O_RDWR | O_NOCTTY | O_EXCL | O_NDELAY);
        if (Fd < 0)
            throw std::runtime_error("can't open serial port");

        if (tcgetattr(Fd, &OldTermios) != 0) {
            throw std::runtime_error("can't get termios attributes " + FormatErrno(errno));
        }

        ApplySerialPortSettings();

    } catch (const std::runtime_error& e) {
        if (Fd >= 0) {
            close(Fd);
            Fd = -1;
        }
        throw TSerialDeviceException(Settings.Device + ", " + e.what());
    }
    LastInteraction = std::chrono::steady_clock::now();
    SkipNoise(); // flush data from previous instance if any
}

void TSerialPort::Close()
{
    if (Base::IsOpen()) {
        tcsetattr(Fd, TCSANOW, &OldTermios);
    }
    Base::Close();
}

void TSerialPort::ApplySerialPortSettings(const TSerialPortSettings* settings)
{
    termios dev;
    MakeTermios(settings != nullptr ? *settings : Settings, dev);

    if (tcsetattr(Fd, TCSANOW, &dev) != 0) {
        throw std::runtime_error("can't set termios attributes" + FormatErrno(errno));
    }
}

std::chrono::milliseconds TSerialPort::GetSendTime(double bytesNumber) const
{
    size_t bitsPerByte = 1 + Settings.DataBits + Settings.StopBits;
    if (Settings.Parity != 'N') {
        ++bitsPerByte;
    }
    auto ms = std::ceil((1000.0 * bitsPerByte * bytesNumber) / double(Settings.BaudRate));
    return std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(ms));
}

uint8_t TSerialPort::ReadByte(const std::chrono::microseconds& timeout)
{
    return Base::ReadByte(timeout + GetLinuxLag(Settings.BaudRate));
}

size_t TSerialPort::ReadFrame(uint8_t* buf,
                              size_t count,
                              const std::chrono::microseconds& responseTimeout,
                              const std::chrono::microseconds& frameTimeout,
                              TFrameCompletePred frameComplete)
{
    return Base::ReadFrame(buf,
                           count,
                           responseTimeout + GetLinuxLag(Settings.BaudRate) + GetSendTime(RxTrigBytes),
                           frameTimeout + std::chrono::milliseconds(15) + GetSendTime(RxTrigBytes),
                           frameComplete);
}

void TSerialPort::WriteBytes(const uint8_t* buf, int count)
{
    Base::WriteBytes(buf, count);
    SleepSinceLastInteraction(GetSendTime(count));
    LastInteraction = std::chrono::steady_clock::now();
}

std::string TSerialPort::GetDescription(bool verbose) const
{
    if (verbose) {
        return Settings.ToString();
    }
    return Settings.Device;
}

const TSerialPortSettings& TSerialPort::GetSettings() const
{
    return Settings;
}

TSerialPortWithIECHack::TSerialPortWithIECHack(PSerialPort port): Port(port), UseIECHack(false)
{}

void TSerialPortWithIECHack::Open()
{
    Port->Open();
}

void TSerialPortWithIECHack::Close()
{
    Port->Close();
}

void TSerialPortWithIECHack::Reopen()
{
    Port->Reopen();
}

bool TSerialPortWithIECHack::IsOpen() const
{
    return Port->IsOpen();
}

void TSerialPortWithIECHack::CheckPortOpen() const
{
    Port->CheckPortOpen();
}

void TSerialPortWithIECHack::WriteBytes(const uint8_t* buf, int count)
{
    if (UseIECHack) {
        std::vector<uint8_t> buf_8bit(IEC::SetEvenParity(buf, count));
        Port->WriteBytes(buf_8bit.data(), count);
    } else {
        Port->WriteBytes(buf, count);
    }
}

uint8_t TSerialPortWithIECHack::ReadByte(const std::chrono::microseconds& timeout)
{
    auto c = Port->ReadByte(timeout);
    if (UseIECHack) {
        IEC::CheckStripEvenParity(&c, 1);
    }
    return c;
}

size_t TSerialPortWithIECHack::ReadFrame(uint8_t* buf,
                                         size_t count,
                                         const std::chrono::microseconds& responseTimeout,
                                         const std::chrono::microseconds& frameTimeout,
                                         TFrameCompletePred frameComplete)
{
    if (UseIECHack) {
        auto wrappedFrameComplete = [=](uint8_t* buf, size_t count) {
            std::vector<uint8_t> b(buf, buf + count);
            IEC::CheckStripEvenParity(b.data(), count);
            return frameComplete(b.data(), count);
        };
        auto l = Port->ReadFrame(buf, count, responseTimeout, frameTimeout, wrappedFrameComplete);
        IEC::CheckStripEvenParity(buf, l);
        return l;
    }
    return Port->ReadFrame(buf, count, responseTimeout, frameTimeout, frameComplete);
}

void TSerialPortWithIECHack::SkipNoise()
{
    Port->SkipNoise();
}

void TSerialPortWithIECHack::SleepSinceLastInteraction(const std::chrono::microseconds& us)
{
    Port->SleepSinceLastInteraction(us);
}

std::chrono::milliseconds TSerialPortWithIECHack::GetSendTime(double bytesNumber) const
{
    return Port->GetSendTime(bytesNumber);
}

std::string TSerialPortWithIECHack::GetDescription(bool verbose) const
{
    return Port->GetDescription(verbose);
}

void TSerialPortWithIECHack::SetSerialPortByteFormat(const TSerialPortByteFormat* params)
{
    if (params == nullptr) {
        UseIECHack = false;
        return;
    }

    if (Port->GetSettings().DataBits == 8 && Port->GetSettings().Parity == 'N' && Port->GetSettings().StopBits == 1 &&
        params->DataBits == 7 && params->Parity == 'E' && params->StopBits == 1)
    {
        UseIECHack = true;
        return;
    }

    if (Port->GetSettings().DataBits == 7 && Port->GetSettings().Parity == 'E' && Port->GetSettings().StopBits == 1 &&
        params->DataBits == 7 && params->Parity == 'E' && params->StopBits == 1)
    {
        UseIECHack = false;
        return;
    }
    throw std::runtime_error("Can't change " + Port->GetSettings().ToString() +
                             " byte format. Set port settings to 8N1, please");
}
