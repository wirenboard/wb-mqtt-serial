# http://make.mad-scientist.net/papers/advanced-auto-dependency-generation/
DEPDIR := .d
$(shell mkdir -p $(DEPDIR) >/dev/null)
DEPFLAGS = -std=c++0x -MT $@ -MMD -MP -MF $(DEPDIR)/$(notdir $*.Td)

POSTCOMPILE = mv -f $(DEPDIR)/$(notdir $*.Td) $(DEPDIR)/$(notdir $*.d)

ifeq ($(DEB_TARGET_ARCH),armel)
CROSS_COMPILE=arm-linux-gnueabi-
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

#CFLAGS=-Wall -ggdb -std=c++0x -O0 -I.
CFLAGS= -Wall -std=c++0x -Os -I.
LDFLAGS= -pthread -lmosquittopp -lmosquitto -ljsoncpp -lwbmqtt

MODBUS_BIN=wb-homa-modbus
MODBUS_LIBS=-lmodbus
MODBUS_SRCS=modbus_client.c \
  modbus_config.c \
  modbus_port.c \
  modbus_observer.c \
  serial_protocol.c \
  serial_connector.c \
  uniel_protocol.c \
  ivtm_protocol.c \
  crc16.c \
  em_protocol.c \
  milur_protocol.c \
  mercury230_protocol.c
MODBUS_OBJS=$(MODBUS_SRCS:.c=.o)
TEST_SRCS= \
  $(TEST_DIR)/testlog.o \
  $(TEST_DIR)/modbus_test.o \
  $(TEST_DIR)/em_test.o \
  $(TEST_DIR)/em_integration.o \
  $(TEST_DIR)/ivtm_test.o \
  $(TEST_DIR)/fake_modbus.o \
  $(TEST_DIR)/fake_mqtt.o \
  $(TEST_DIR)/fake_serial_port.o \
  $(TEST_DIR)/main.o
TEST_OBJS=$(TEST_SRCS:.c=.o)
TEST_LIBS=-lgtest -lpthread -lmosquittopp
TEST_DIR=test
TEST_BIN=wb-homa-test
SRCS=$(MODBUS_SRCS) $(TEST_SRCS)

.PHONY: all clean test

all : $(MODBUS_BIN)

# Modbus
%.o : %.cpp $(DEPDIR)/$(notdir %.d)
	${CXX} ${DEPFLAGS} -c $< -o $@ ${CFLAGS}
	$(POSTCOMPILE)

test/%.o : test/%.cpp $(DEPDIR)/$(notdir %.d)
	${CXX} ${DEPFLAGS} -c $< -o $@ ${CFLAGS}
	$(POSTCOMPILE)

$(MODBUS_BIN) : main.o $(MODBUS_OBJS)
	${CXX} $^ ${LDFLAGS} -o $@ $(MODBUS_LIBS)

$(TEST_DIR)/$(TEST_BIN): $(MODBUS_OBJS) $(TEST_OBJS)
	${CXX} $^ ${LDFLAGS} -o $@ $(TEST_LIBS) $(MODBUS_LIBS)

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
	-rm -rf *.o $(MODBUS_BIN) $(DEPDIR)
	-rm -f $(TEST_DIR)/*.o $(TEST_DIR)/$(TEST_BIN)


install: all
	mkdir -p $(DESTDIR)/etc/wb-mqtt-confed/schemas
	mkdir -p $(DESTDIR)/etc/wb-configs.d
	install -d $(DESTDIR)
	install -d $(DESTDIR)/etc
	install -d $(DESTDIR)/usr/bin
	install -d $(DESTDIR)/usr/lib
	install -d $(DESTDIR)/usr/share/wb-homa-modbus

	install -m 0644  config.json $(DESTDIR)/etc/wb-homa-modbus.conf.sample
	install -m 0644  config.default.json $(DESTDIR)/etc/wb-homa-modbus.conf
	install -m 0644  wb-homa-modbus.wbconfigs $(DESTDIR)/etc/wb-configs.d/11wb-homa-modbus

	install -m 0644  wb-homa-modbus.schema.json $(DESTDIR)/etc/wb-mqtt-confed/schemas/wb-homa-modbus.schema.json
	install -m 0755  $(MODBUS_BIN) $(DESTDIR)/usr/bin/$(MODBUS_BIN)
	cp -r  wb-homa-modbus-templates $(DESTDIR)/usr/share/wb-homa-modbus/templates

$(DEPDIR)/$(notdir %.d): ;

-include $(patsubst %,$(DEPDIR)/%.d,$(notdir $(basename $(SRCS))))
