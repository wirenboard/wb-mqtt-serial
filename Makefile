#CFLAGS=
DEBUG_CFLAGS=-Wall -ggdb -std=c++14 -O0 -I./src
NORMAL_CFLAGS=-Wall -std=c++14 -O3 -I./src
CFLAGS=$(if $(or $(DEBUG)), $(DEBUG_CFLAGS),$(NORMAL_CFLAGS))
LDFLAGS= -pthread -ljsoncpp -lwbmqtt1

SERIAL_BIN=wb-mqtt-serial
SERIAL_LIBS=
SERIAL_SRCS= \
  src/register.cpp                    \
  src/poll_plan.cpp                   \
  src/serial_client.cpp               \
  src/register_handler.cpp            \
  src/serial_config.cpp               \
  src/serial_port_driver.cpp          \
  src/serial_driver.cpp               \
  src/serial_port.cpp                 \
  src/serial_device.cpp               \
  src/crc16.cpp                       \
  src/modbus_common.cpp               \
  src/bcd_utils.cpp                   \
  src/log.cpp                         \
  src/tcp_port.cpp                    \
  src/file_descriptor_port.cpp        \
  src/devices/mercury200_device.cpp   \
  src/devices/uniel_device.cpp        \
  src/devices/s2k_device.cpp          \
  src/devices/ivtm_device.cpp         \
  src/devices/pulsar_device.cpp       \
  src/devices/modbus_device.cpp       \
  src/devices/modbus_io_device.cpp    \
  src/devices/em_device.cpp           \
  src/devices/milur_device.cpp        \
  src/devices/mercury230_device.cpp   \

SERIAL_OBJS=$(SERIAL_SRCS:.cpp=.o)

TEST_SRCS= \
  $(TEST_DIR)/poll_plan_test.o                        \
  $(TEST_DIR)/serial_client_test.o                    \
  $(TEST_DIR)/modbus_expectations_base.o              \
  $(TEST_DIR)/modbus_expectations.o                   \
  $(TEST_DIR)/modbus_test.o                           \
  $(TEST_DIR)/modbus_io_expectations.o                \
  $(TEST_DIR)/modbus_io_test.o                        \
  $(TEST_DIR)/uniel_expectations.o                    \
  $(TEST_DIR)/uniel_test.o                            \
  $(TEST_DIR)/s2k_expectations.o                      \
  $(TEST_DIR)/s2k_test.o                              \
  $(TEST_DIR)/em_test.o                               \
  $(TEST_DIR)/em_integration.o                        \
  $(TEST_DIR)/mercury200_expectations.o               \
  $(TEST_DIR)/mercury200_test.o                       \
  $(TEST_DIR)/mercury230_expectations.o               \
  $(TEST_DIR)/mercury230_test.o                       \
  $(TEST_DIR)/milur_expectations.o                    \
  $(TEST_DIR)/milur_test.o                            \
  $(TEST_DIR)/ivtm_test.o                             \
  $(TEST_DIR)/pulsar_test.o                           \
  $(TEST_DIR)/fake_serial_port.o                      \
  $(TEST_DIR)/fake_serial_device.o                    \
  $(TEST_DIR)/device_templates_file_extension_test.o  \
  $(TEST_DIR)/reconnect_detection_test.o              \
  $(TEST_DIR)/main.o                                  \

TEST_OBJS=$(TEST_SRCS:.cpp=.o)
TEST_LIBS=-lgtest -lpthread -lmosquittopp -lwbmqtt_test_utils
TEST_DIR=test
TEST_BIN=wb-homa-test
SRCS=$(SERIAL_SRCS) $(TEST_SRCS)

.PHONY: all clean test

all : $(SERIAL_BIN)

# Modbus
%.o : %.cpp
	${CXX} -c $< -o $@ ${CFLAGS}
	$(POSTCOMPILE)

test/%.o : test/%.cpp
	${CXX} -c $< -o $@ ${CFLAGS}
	$(POSTCOMPILE)

$(SERIAL_BIN) : src/main.o $(SERIAL_OBJS)
	${CXX} $^ ${LDFLAGS} -o $@ $(SERIAL_LIBS)

$(TEST_DIR)/$(TEST_BIN): $(SERIAL_OBJS) $(TEST_OBJS)
	${CXX} $^ ${LDFLAGS} -o $@ $(TEST_LIBS) $(SERIAL_LIBS)

test: $(TEST_DIR)/$(TEST_BIN)
	rm -f $(TEST_DIR)/*.dat.out
	if [ "$(shell arch)" = "armv7l" ]; then \
        $(TEST_DIR)/$(TEST_BIN) $(TEST_ARGS) || { $(TEST_DIR)/abt.sh show; exit 1; } \
    else \
		valgrind --error-exitcode=180 -q $(TEST_DIR)/$(TEST_BIN) $(TEST_ARGS) || \
		if [ $$? = 180 ]; then \
			echo "*** VALGRIND DETECTED ERRORS ***" 1>& 2; \
			exit 1; \
		else $(TEST_DIR)/abt.sh show; exit 1; fi; \
	fi

clean :
	-rm -rf src/*.o src/devices/*.o $(SERIAL_BIN)
	-rm -f $(TEST_DIR)/*.o $(TEST_DIR)/$(TEST_BIN)


install: all
	install -d $(DESTDIR)/usr/share/wb-mqtt-confed/schemas
	install -d $(DESTDIR)/var/lib/wb-mqtt-serial

	install -D -m 0644  data/config.sample.json $(DESTDIR)/etc/wb-mqtt-serial.conf.sample

	install -D -m 0644  data/config.json.wb234 $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.wb234
	install -D -m 0644  data/config.json.wb5 $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.wb5
	install -D -m 0644  data/config.json.wb6 $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.wb5
	install -D -m 0644  data/config.json.default $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.default

	install -D -m 0644  wb-mqtt-serial.wbconfigs $(DESTDIR)/etc/wb-configs.d/11wb-mqtt-serial

	install -D -m 0644  data/wb-mqtt-serial.schema.json $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial.schema.json
	install -D -m 0644  data/wb-mqtt-serial-device-template.schema.json $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial-device-template.schema.json
	cp -r  data/wb-mqtt-serial-templates $(DESTDIR)/usr/share/wb-mqtt-serial/templates

	install -m 0755  $(SERIAL_BIN) $(DESTDIR)/usr/bin/$(SERIAL_BIN)
