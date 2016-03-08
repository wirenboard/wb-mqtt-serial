#include <stdexcept>
#include "modbus_server.h"

namespace {
    const int SELECT_PERIOD_MS = 50;
}

TModbusServer::TModbusServer(PSerialPort port):
    Stop(false),
    Port(port),
    Slave(new TModbusSlave(1, TModbusRange(TServerRegisterRange(0, 1),
                                           TServerRegisterRange(0, 1),
                                           TServerRegisterRange(0, 1),
                                           TServerRegisterRange(0, 1)))),
    Query(new uint8_t[MODBUS_RTU_MAX_ADU_LENGTH]) {}

TModbusServer::~TModbusServer()
{
    {
        std::unique_lock<std::mutex> lk(Mutex);
        Stop = true;
    }
    ServerThread.join();
    delete[] Query;
}

void TModbusServer::Start()
{
    Port->Open();
    modbus_set_slave(Port->LibModbusContext()->Inner, Slave->Address);
    ServerThread = std::thread(&TModbusServer::Run, std::ref(*this));
}

PModbusSlave TModbusServer::GetSlave() const
{
    return Slave;
}

PModbusSlave TModbusServer::SetSlave(PModbusSlave slave)
{
    Slave = slave;
    return Slave;
}

PModbusSlave TModbusServer::SetSlave(int slave_addr, const TModbusRange& range)
{
    return SetSlave(PModbusSlave(new TModbusSlave(slave_addr, range)));
}

bool TModbusServer::IsRequestBlacklisted() const
{
    // FIXME: currently we only check for holding registers
    int header_length = modbus_get_header_length(Port->LibModbusContext()->Inner),
        ref = MODBUS_GET_INT16_FROM_INT8(Query, header_length + 1),
        count;
    switch (Query[header_length]) {
    case 0x03: // read holding registers
        count = MODBUS_GET_INT16_FROM_INT8(Query, header_length + 3);
        while (count--) {
            if (Slave->Holding.IsReadBlacklisted(ref++))
                return true;
        }
        break;
    case 0x06: // write single register
        if (Slave->Holding.IsWriteBlacklisted(ref))
            return true;
        break;
    case 0x10: // write multiple registers
        count = MODBUS_GET_INT16_FROM_INT8(Query, header_length + 3);
        while (count--) {
            if (Slave->Holding.IsWriteBlacklisted(ref++))
                return true;
        }
        break;
    }
    return false;
}

void TModbusServer::Run()
{
    for (;;) {
        bool got_data = Port->Select(SELECT_PERIOD_MS);
        {
            std::unique_lock<std::mutex> lk(Mutex);
            if (Stop)
                break;
        }
        if (!got_data)
            continue;
        int rc = modbus_receive(Port->LibModbusContext()->Inner, Query);
        if (rc < 0) {
            // XXX: this may happen due to port being closed.
            // read() may give EIO in this case which will be recognized
            // as an error by libmodbus.
            // throw std::runtime_error("modbus_receive() failed");
            break;
        }
        rc = IsRequestBlacklisted() ?
            modbus_reply_exception(Port->LibModbusContext()->Inner, Query,
                                   MODBUS_EXCEPTION_SLAVE_OR_SERVER_BUSY) :
            modbus_reply(Port->LibModbusContext()->Inner, Query, rc, Slave->Mapping);
        if (rc < 0)
            throw std::runtime_error("modbus_reply() / modbus_reply_exception() failed");
    }
}
