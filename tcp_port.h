#pragma once

#include "file_descriptor_port.h"
#include "tcp_port_settings.h"


class TTcpPort: public TFileDescriptorPort
{
public:
    TTcpPort(const PTcpPortSettings & settings);
    ~TTcpPort() = default;

    void Open() override;
    
private:
    PTcpPortSettings Settings;
};
