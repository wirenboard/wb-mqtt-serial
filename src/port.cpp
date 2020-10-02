#include "port.h"

void TPort::CycleBegin()
{}

void TPort::CycleEnd(bool ok)
{}

void TPort::Reopen()
{
    if (IsOpen()) {
        Close();
        Open(); 
    }
}

void TPort::WriteBytes(const std::vector<uint8_t>& buf)
{
    WriteBytes(&buf[0], buf.size());
}

void TPort::WriteBytes(const std::string& buf)
{
    WriteBytes(reinterpret_cast<const uint8_t*>(buf.c_str()), buf.size());
}

std::chrono::milliseconds TPort::GetSendTime(double bytesNumber)
{
    return std::chrono::milliseconds::zero();
}

void TPort::SetSerialPortByteFormat(const TSerialPortByteFormat* params)
{}
