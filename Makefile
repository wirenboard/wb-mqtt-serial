# http://make.mad-scientist.net/papers/advanced-auto-dependency-generation/
DEPDIR := .d
$(shell mkdir -p $(DEPDIR) >/dev/null)
DEPFLAGS = -std=c++0x -MT $@ -MMD -MP -MF $(DEPDIR)/$(notdir $*.Td)

POSTCOMPILE = mv -f $(DEPDIR)/$(notdir $*.Td) $(DEPDIR)/$(notdir $*.d)

ifeq ($(DEB_TARGET_ARCH),armel)
CROSS_COMPILE=arm-linux-gnueabi-
endif

#brainroot add for cross-compile
ifeq ($(DEB_TARGET_ARCH),armhf)
CROSS_COMPILE=arm-linux-gnueabihf-
endif

CXX=$(CROSS_COMPILE)g++
CXX_PATH := $(shell which $(CROSS_COMPILE)g++-4.7)

CC=$(CROSS_COMPILE)gcc
CC_PATH := $(shell which $(CROSS_COMPILE)gcc-4.7)

ifneq ($(CXX_PATH),)
	CXX=$(CROSS_COMPILE)g++-4.7
endif

ifneq ($(CC_PATH),)
	CC=$(CROSS_COMPILE)gcc-4.7
endif

#CFLAGS=
DEBUG_CFLAGS=-Wall -ggdb -std=c++0x -O0 -I.
NORMAL_CFLAGS=-Wall -std=c++0x -Os -I.
CFLAGS=$(if $(or $(DEBUG),$(filter test, $(MAKECMDGOALS))), $(DEBUG_CFLAGS),$(NORMAL_CFLAGS))
LDFLAGS= -pthread -lmosquittopp -lmosquitto -ljsoncpp -lwbmqtt

SERIAL_BIN=wb-mqtt-serial
SERIAL_LIBS=
SERIAL_SRCS=register.cpp \
  poll_plan.cpp \
  serial_client.cpp \
  register_handler.cpp \
  serial_config.cpp \
  serial_port_driver.cpp \
  serial_observer.cpp \
  file_descriptor_port.cpp \
  tcp_port.cpp \
  serial_port.cpp \
  serial_device.cpp \
  uniel_device.cpp \
  s2k_device.cpp \
  ivtm_device.cpp \
  lls_device.cpp \
  crc16.cpp \
  modbus_common.cpp \
  modbus_device.cpp \
  modbus_io_device.cpp \
  em_device.cpp \
  milur_device.cpp \
  mercury230_device.cpp \
  mercury200_device.cpp \
  pulsar_device.cpp \
  bcd_utils.cpp
SERIAL_OBJS=$(SERIAL_SRCS:.cpp=.o)
TEST_SRCS= \
  $(TEST_DIR)/testlog.o \
  $(TEST_DIR)/poll_plan_test.o \
  $(TEST_DIR)/serial_client_test.o \
  $(TEST_DIR)/modbus_expectations_base.o \
  $(TEST_DIR)/modbus_expectations.o \
  $(TEST_DIR)/modbus_test.o \
  $(TEST_DIR)/modbus_io_expectations.o \
  $(TEST_DIR)/modbus_io_test.o \
  $(TEST_DIR)/uniel_expectations.o \
  $(TEST_DIR)/uniel_test.o \
  $(TEST_DIR)/s2k_expectations.o \
  $(TEST_DIR)/s2k_test.o \
  $(TEST_DIR)/em_test.o \
  $(TEST_DIR)/em_integration.o \
  $(TEST_DIR)/mercury200_expectations.o \
  $(TEST_DIR)/mercury200_test.o \
  $(TEST_DIR)/lls_test.o \
  $(TEST_DIR)/mercury230_expectations.o \
  $(TEST_DIR)/mercury230_test.o \
  $(TEST_DIR)/milur_expectations.o \
  $(TEST_DIR)/milur_test.o \
  $(TEST_DIR)/ivtm_test.o \
  $(TEST_DIR)/pulsar_test.o \
  $(TEST_DIR)/fake_mqtt.o \
  $(TEST_DIR)/fake_serial_port.o \
  $(TEST_DIR)/fake_serial_device.o \
  $(TEST_DIR)/device_templates_file_extension_test.o \
  $(TEST_DIR)/main.o
TEST_OBJS=$(TEST_SRCS:.cpp=.o)
TEST_LIBS=-lgtest -lpthread -lmosquittopp
TEST_DIR=test
TEST_BIN=wb-homa-test
SRCS=$(SERIAL_SRCS) $(TEST_SRCS)

.PHONY: all clean test

all : $(SERIAL_BIN)

# Modbus
%.o : %.cpp $(DEPDIR)/$(notdir %.d)
	${CXX} ${DEPFLAGS} -c $< -o $@ ${CFLAGS}
	$(POSTCOMPILE)

test/%.o : test/%.cpp $(DEPDIR)/$(notdir %.d)
	${CXX} ${DEPFLAGS} -c $< -o $@ ${CFLAGS}
	$(POSTCOMPILE)

$(SERIAL_BIN) : main.o $(SERIAL_OBJS)
	${CXX} $^ ${LDFLAGS} -o $@ $(SERIAL_LIBS)

$(TEST_DIR)/$(TEST_BIN): $(SERIAL_OBJS) $(TEST_OBJS)
	${CXX} $^ ${LDFLAGS} -o $@ $(TEST_LIBS) $(SERIAL_LIBS)

test: $(TEST_DIR)/$(TEST_BIN)
	# cannot run valgrind under qemu chroot
	rm -f $(TEST_DIR)/*.dat.out
	if [ "$(shell arch)" = "armv7l" ]; then \
          $(TEST_DIR)/$(TEST_BIN) $(TEST_ARGS) || $(TEST_DIR)/abt.sh show; \
        else \
          valgrind --error-exitcode=180 -q $(TEST_DIR)/$(TEST_BIN) $(TEST_ARGS) || \
            if [ $$? = 180 ]; then \
              echo "*** VALGRIND DETECTED ERRORS ***" 1>& 2; \
              exit 1; \
            else $(TEST_DIR)/abt.sh show; exit 1; fi; \
        fi

clean :
	-rm -rf *.o $(SERIAL_BIN) $(DEPDIR)
	-rm -f $(TEST_DIR)/*.o $(TEST_DIR)/$(TEST_BIN)


install: all
	mkdir -p $(DESTDIR)/usr/share/wb-mqtt-confed/schemas
	mkdir -p $(DESTDIR)/etc/wb-configs.d
	install -d $(DESTDIR)
	install -d $(DESTDIR)/etc
	install -d $(DESTDIR)/usr/bin
	install -d $(DESTDIR)/usr/lib
	install -d $(DESTDIR)/usr/share/wb-mqtt-serial

	install -m 0644  config.sample.json $(DESTDIR)/etc/wb-mqtt-serial.conf.sample

	install -m 0644  config.json.wb234 $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.wb234
	install -m 0644  config.json.wb5 $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.wb5
	install -m 0644  config.json.wb6 $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.wb6
	install -m 0644  config.json.default $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.default


	install -m 0644  wb-mqtt-serial.wbconfigs $(DESTDIR)/etc/wb-configs.d/11wb-mqtt-serial

	install -m 0644  wb-mqtt-serial.schema.json $(DESTDIR)/usr/share/wb-mqtt-confed/schemas/wb-mqtt-serial.schema.json
	install -m 0755  $(SERIAL_BIN) $(DESTDIR)/usr/bin/$(SERIAL_BIN)
	cp -r  wb-mqtt-serial-templates $(DESTDIR)/usr/share/wb-mqtt-serial/templates

$(DEPDIR)/$(notdir %.d): ;

-include $(patsubst %,$(DEPDIR)/%.d,$(notdir $(basename $(SRCS))))
