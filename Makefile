ifneq ($(DEB_HOST_MULTIARCH),)
	CROSS_COMPILE ?= $(DEB_HOST_MULTIARCH)-
endif

ifeq ($(origin CC),default)
	CC := $(CROSS_COMPILE)gcc
endif
ifeq ($(origin CXX),default)
	CXX := $(CROSS_COMPILE)g++
endif

GIT_REVISION:=$(shell git rev-parse HEAD)
DEB_VERSION:=$(shell head -1 debian/changelog | awk '{ print $$2 }' | sed 's/[\(\)]//g')

DEBUG_CFLAGS=-Wall -ggdb -std=c++14 -O0 -I./src
NORMAL_CFLAGS=-Wall -Werror -std=c++14 -O3 -I./src
CFLAGS=$(if $(or $(DEBUG)), $(DEBUG_CFLAGS),$(NORMAL_CFLAGS)) -DWBMQTT_COMMIT="$(GIT_REVISION)" -DWBMQTT_VERSION="$(DEB_VERSION)"
LDFLAGS= -lpthread -ljsoncpp -lwbmqtt1

SERIAL_BIN=wb-mqtt-serial
SERIAL_SRCS= \
  src/port.cpp                           \
  src/register.cpp                       \
  src/poll_plan.cpp                      \
  src/serial_client.cpp                  \
  src/register_handler.cpp               \
  src/serial_config.cpp                  \
  src/confed_schema_generator.cpp        \
  src/confed_json_generator.cpp          \
  src/confed_config_generator.cpp        \
  src/config_merge_template.cpp          \
  src/serial_port_driver.cpp             \
  src/serial_driver.cpp                  \
  src/serial_port.cpp                    \
  src/serial_device.cpp                  \
  src/crc16.cpp                          \
  src/modbus_common.cpp                  \
  src/iec_common.cpp                     \
  src/bcd_utils.cpp                      \
  src/log.cpp                            \
  src/file_descriptor_port.cpp           \
  src/tcp_port.cpp                       \
  src/devices/mercury200_device.cpp      \
  src/devices/uniel_device.cpp           \
  src/devices/s2k_device.cpp             \
  src/devices/ivtm_device.cpp            \
  src/devices/pulsar_device.cpp          \
  src/devices/modbus_device.cpp          \
  src/devices/modbus_io_device.cpp       \
  src/devices/milur_device.cpp           \
  src/devices/mercury230_device.cpp      \
  src/devices/lls_device.cpp             \
  src/devices/em_device.cpp              \
  src/devices/energomera_iec_device.cpp  \
  src/devices/neva_device.cpp            \
  src/file_utils.cpp                     \

SERIAL_OBJS=$(SERIAL_SRCS:.cpp=.o)

TEST_SRCS= \
  $(TEST_DIR)/expector.o                              \
  $(TEST_DIR)/poll_plan_test.o                        \
  $(TEST_DIR)/serial_client_test.o                    \
  $(TEST_DIR)/serial_config_test.o                    \
  $(TEST_DIR)/modbus_expectations_base.o              \
  $(TEST_DIR)/modbus_expectations.o                   \
  $(TEST_DIR)/modbus_test.o                           \
  $(TEST_DIR)/modbus_io_expectations.o                \
  $(TEST_DIR)/modbus_io_test.o                        \
  $(TEST_DIR)/modbus_tcp_test.o                       \
  $(TEST_DIR)/uniel_expectations.o                    \
  $(TEST_DIR)/uniel_test.o                            \
  $(TEST_DIR)/s2k_expectations.o                      \
  $(TEST_DIR)/s2k_test.o                              \
  $(TEST_DIR)/em_test.o                               \
  $(TEST_DIR)/em_integration.o                        \
  $(TEST_DIR)/mercury200_expectations.o               \
  $(TEST_DIR)/mercury200_test.o                       \
  $(TEST_DIR)/lls_test.o                              \
  $(TEST_DIR)/mercury230_expectations.o               \
  $(TEST_DIR)/mercury230_test.o                       \
  $(TEST_DIR)/milur_expectations.o                    \
  $(TEST_DIR)/milur_test.o                            \
  $(TEST_DIR)/ivtm_test.o                             \
  $(TEST_DIR)/pulsar_test.o                           \
  $(TEST_DIR)/fake_serial_port.o                      \
  $(TEST_DIR)/fake_serial_device.o                    \
  $(TEST_DIR)/device_templates_file_extension_test.o  \
  $(TEST_DIR)/pty_based_fake_serial.o                 \
  $(TEST_DIR)/serial_port_test.o                      \
  $(TEST_DIR)/iec_test.o                              \
  $(TEST_DIR)/neva_test.o                             \
  $(TEST_DIR)/energomera_test.o                       \
  $(TEST_DIR)/main.o                                  \

TEST_OBJS=$(TEST_SRCS:.cpp=.o)
TEST_LIBS=-lgtest -lwbmqtt_test_utils
TEST_DIR=test
TEST_BIN=wb-homa-test
SRCS=$(SERIAL_SRCS) $(TEST_SRCS)

.PHONY: all clean test

all : $(SERIAL_BIN)

# Modbus
%.o : %.cpp
	${CXX} -c $< -o $@ ${CFLAGS}

test/%.o : test/%.cpp
	${CXX} -c $< -o $@ ${CFLAGS}

$(SERIAL_BIN) : src/main.o $(SERIAL_OBJS)
	${CXX} $^ ${LDFLAGS} -o $@ 

$(TEST_DIR)/$(TEST_BIN): $(SERIAL_OBJS) $(TEST_OBJS)
	${CXX} $^ ${LDFLAGS} $(TEST_LIBS) -o $@ -fno-lto

test: $(TEST_DIR)/$(TEST_BIN)
	rm -f $(TEST_DIR)/*.dat.out
	if [ "$(shell arch)" != "armv7l" ] && [ "$(CROSS_COMPILE)" = "" ] || [ "$(CROSS_COMPILE)" = "x86_64-linux-gnu-" ]; then \
		valgrind --error-exitcode=180 -q $(TEST_DIR)/$(TEST_BIN) $(TEST_ARGS) || \
		if [ $$? = 180 ]; then \
			echo "*** VALGRIND DETECTED ERRORS ***" 1>& 2; \
			exit 1; \
		else $(TEST_DIR)/abt.sh show; exit 1; fi; \
    else \
        $(TEST_DIR)/$(TEST_BIN) $(TEST_ARGS) || { $(TEST_DIR)/abt.sh show; exit 1; } \
	fi

clean :
	-rm -rf src/*.o src/devices/*.o $(SERIAL_BIN)
	-rm -f $(TEST_DIR)/*.o $(TEST_DIR)/$(TEST_BIN)


install:
	install -d $(DESTDIR)/usr/share/wb-mqtt-confed/schemas
	install -d $(DESTDIR)/var/lib/wb-mqtt-serial
	install -d $(DESTDIR)/etc/wb-mqtt-serial.conf.d/templates

	install -D -m 0644  config.sample.json $(DESTDIR)/etc/wb-mqtt-serial.conf.sample

	install -D -m 0644  config.json.wb234 $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.wb234
	install -D -m 0644  config.json.wb5 $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.wb5
	install -D -m 0644  config.json.wb6 $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.wb6
	install -D -m 0644  config.json.default $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.default

	install -D -m 0644  wb-mqtt-serial.wbconfigs $(DESTDIR)/etc/wb-configs.d/11wb-mqtt-serial

	install -D -m 0644  wb-mqtt-serial.schema.json $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial.schema.json
	install -D -m 0644  wb-mqtt-serial-device-template.schema.json $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial-device-template.schema.json
	cp -r  wb-mqtt-serial-templates $(DESTDIR)/usr/share/wb-mqtt-serial/templates

	install -D -m 0755  $(SERIAL_BIN) $(DESTDIR)/usr/bin/$(SERIAL_BIN)
