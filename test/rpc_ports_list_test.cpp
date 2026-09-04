#include <gtest/gtest.h>
#include <wblib/testing/testlog.h>

#include "rpc/rpc_helpers.h"
#include "serial_config.h"
#include "test_utils.h"

using WBMQTT::Testing::TLoggedFixture;

namespace
{
    const std::string CONFIG_PATH = "configs/ports-list-test.json";

    //! Indexes of the ports of CONFIG_PATH in the response, the disabled port is not reported
    const Json::ArrayIndex SERIAL_PORT = 0;
    const Json::ArrayIndex SERIAL_PORT_WITH_DEFAULTS = 1;
    const Json::ArrayIndex MODBUS_TCP_PORT = 2;
    const Json::ArrayIndex TCP_PORT = 3;
}

class TRPCPortsListTest: public testing::Test
{
protected:
    TSerialDeviceFactory DeviceFactory;

    void SetUp() override
    {
        RegisterProtocols(DeviceFactory);
    }

    Json::Value GetPorts()
    {
        auto response = MakePortsListResponse(*LoadTestConfig(CONFIG_PATH, DeviceFactory));
        EXPECT_TRUE(response["ports"].isArray());
        return response["ports"];
    }

    Json::Value GetPort(Json::ArrayIndex index)
    {
        auto ports = GetPorts();
        EXPECT_GT(ports.size(), index);
        return ports[index];
    }

    //! Finds a device by its identifier on any port, returns null if there is no such device
    Json::Value FindDevice(const std::string& deviceId)
    {
        for (const auto& port: GetPorts()) {
            for (const auto& device: port["devices"]) {
                if (device["device_id"].asString() == deviceId) {
                    return device;
                }
            }
        }
        return Json::Value();
    }
};

//! The whole response, so that the format of the public method is fixed by the test
TEST_F(TRPCPortsListTest, Response)
{
    auto expected = ParseJson(R"({
        "ports": [
            {
                "path": "/dev/ttyRS485-1",
                "baud_rate": 19200,
                "data_bits": 7,
                "parity": "E",
                "stop_bits": 2,
                "devices": [
                    {
                        "device_id": "msu34tlp_2",
                        "slave_id": "2",
                        "device_type": "MSU34",
                        "protocol": "modbus"
                    },
                    {
                        "device_id": "custom-modbus",
                        "slave_id": "0x0A",
                        "protocol": "modbus"
                    },
                    {
                        "device_id": "custom-uniel",
                        "slave_id": "1",
                        "protocol": "uniel"
                    },
                    {
                        "device_id": "mercury230ar02",
                        "device_type": "mercury230",
                        "protocol": "mercury230"
                    }
                ]
            },
            {
                "path": "/dev/ttyRS485-2",
                "baud_rate": 9600,
                "data_bits": 8,
                "parity": "N",
                "stop_bits": 1,
                "devices": []
            },
            {
                "ip": "192.168.1.10",
                "port": 502,
                "mode": "modbus-tcp",
                "devices": [
                    {
                        "device_id": "msu34tlp_3",
                        "slave_id": "3",
                        "device_type": "MSU34",
                        "protocol": "modbus"
                    }
                ]
            },
            {
                "ip": "192.168.1.20",
                "port": 2000,
                "devices": [
                    {
                        "device_id": "serial-over-tcp",
                        "slave_id": "7",
                        "protocol": "modbus"
                    }
                ]
            }
        ]
    })");

    auto ports = GetPorts();
    ASSERT_EQ(expected["ports"].size(), ports.size());
    // Compare port by port, so that a failure points at the port it happened in
    for (Json::ArrayIndex i = 0; i < ports.size(); ++i) {
        SCOPED_TRACE("port " + std::to_string(i));
        ASSERT_EQ(SerializeJson(expected["ports"][i]), SerializeJson(ports[i]));
    }
}

//! The service ports/Load reports the same ports, without the devices and with "address" for a TCP port
TEST_F(TRPCPortsListTest, PortConfigsResponse)
{
    auto response = MakePortConfigsResponse(*LoadTestConfig(CONFIG_PATH, DeviceFactory));
    ASSERT_EQ(4u, response.size());
    ASSERT_EQ("/dev/ttyRS485-1", response[SERIAL_PORT]["path"].asString());
    ASSERT_EQ("192.168.1.10", response[MODBUS_TCP_PORT]["address"].asString());
    ASSERT_EQ("modbus-tcp", response[MODBUS_TCP_PORT]["mode"].asString());
    ASSERT_FALSE(response[MODBUS_TCP_PORT].isMember("ip"));
    ASSERT_FALSE(response[SERIAL_PORT].isMember("devices"));
}

TEST_F(TRPCPortsListTest, PortsAreInConfigOrder)
{
    auto ports = GetPorts();
    ASSERT_EQ(4u, ports.size());
    ASSERT_EQ("/dev/ttyRS485-1", ports[SERIAL_PORT]["path"].asString());
    ASSERT_EQ("/dev/ttyRS485-2", ports[SERIAL_PORT_WITH_DEFAULTS]["path"].asString());
    ASSERT_EQ("192.168.1.10", ports[MODBUS_TCP_PORT]["ip"].asString());
    ASSERT_EQ("192.168.1.20", ports[TCP_PORT]["ip"].asString());
}

TEST_F(TRPCPortsListTest, SerialPortReportsSettingsFromConfig)
{
    auto port = GetPort(SERIAL_PORT);
    ASSERT_EQ(19200, port["baud_rate"].asInt());
    ASSERT_EQ(7, port["data_bits"].asInt());
    ASSERT_EQ("E", port["parity"].asString());
    ASSERT_EQ(2, port["stop_bits"].asInt());
}

//! All the five fields are reported even if the configuration sets none of them
TEST_F(TRPCPortsListTest, SerialPortReportsDefaultSettings)
{
    auto port = GetPort(SERIAL_PORT_WITH_DEFAULTS);
    ASSERT_EQ("/dev/ttyRS485-2", port["path"].asString());
    ASSERT_EQ(9600, port["baud_rate"].asInt());
    ASSERT_EQ(8, port["data_bits"].asInt());
    ASSERT_EQ("N", port["parity"].asString());
    ASSERT_EQ(1, port["stop_bits"].asInt());
}

TEST_F(TRPCPortsListTest, PortWithoutDevicesHasEmptyDevicesArray)
{
    auto port = GetPort(SERIAL_PORT_WITH_DEFAULTS);
    ASSERT_TRUE(port["devices"].isArray());
    ASSERT_EQ(0u, port["devices"].size());
}

TEST_F(TRPCPortsListTest, ModeIsReportedForModbusTcpPortOnly)
{
    ASSERT_EQ("modbus-tcp", GetPort(MODBUS_TCP_PORT)["mode"].asString());
    ASSERT_FALSE(GetPort(TCP_PORT).isMember("mode"));
    ASSERT_FALSE(GetPort(SERIAL_PORT).isMember("mode"));
}

TEST_F(TRPCPortsListTest, DisabledPortIsNotReported)
{
    for (const auto& port: GetPorts()) {
        ASSERT_NE("/dev/ttyRS485-3", port["path"].asString());
    }
}

TEST_F(TRPCPortsListTest, DisabledDeviceIsNotReported)
{
    ASSERT_TRUE(FindDevice("disabled-device").isNull());
    ASSERT_EQ(4u, GetPort(SERIAL_PORT)["devices"].size());
}

//! The identifier is the same as in MQTT topics, both when the configuration sets it explicitly
//! and when it is built from the template identifier and the slave id
TEST_F(TRPCPortsListTest, DeviceIdIsTheMqttIdentifier)
{
    ASSERT_FALSE(FindDevice("custom-modbus").isNull());
    ASSERT_FALSE(FindDevice("msu34tlp_2").isNull());
}

TEST_F(TRPCPortsListTest, SlaveIdIsReportedAsWrittenInConfig)
{
    ASSERT_EQ("0x0A", FindDevice("custom-modbus")["slave_id"].asString());
    ASSERT_EQ("2", FindDevice("msu34tlp_2")["slave_id"].asString());
}

//! A device configured without a bus address, for broadcast mode, has no slave id to report
TEST_F(TRPCPortsListTest, SlaveIdIsNotReportedForDeviceWithoutAddress)
{
    auto device = FindDevice("mercury230ar02");
    ASSERT_FALSE(device.isNull());
    ASSERT_FALSE(device.isMember("slave_id"));
}

TEST_F(TRPCPortsListTest, DeviceTypeIsReportedForTemplateDevicesOnly)
{
    ASSERT_EQ("MSU34", FindDevice("msu34tlp_2")["device_type"].asString());
    ASSERT_FALSE(FindDevice("custom-modbus").isMember("device_type"));
    ASSERT_FALSE(FindDevice("custom-uniel").isMember("device_type"));
}

TEST_F(TRPCPortsListTest, ProtocolIsTakenFromTemplateOrDeviceConfig)
{
    ASSERT_EQ("mercury230", FindDevice("mercury230ar02")["protocol"].asString());
    ASSERT_EQ("uniel", FindDevice("custom-uniel")["protocol"].asString());
    // The MSU34 template and the custom device declare no protocol, modbus is the default
    ASSERT_EQ("modbus", FindDevice("msu34tlp_2")["protocol"].asString());
    ASSERT_EQ("modbus", FindDevice("custom-modbus")["protocol"].asString());
}

//! The suffix describes the port transport, the client determines it by the port "mode" field
TEST_F(TRPCPortsListTest, ProtocolHasNoTcpTransportSuffix)
{
    ASSERT_EQ("modbus", FindDevice("msu34tlp_3")["protocol"].asString());
}
