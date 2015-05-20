CXX=g++
CXX_PATH := $(shell which g++-4.7)

CC=gcc
CC_PATH := $(shell which gcc-4.7)

ifneq ($(CXX_PATH),)
	CXX=g++-4.7
endif

ifneq ($(CC_PATH),)
	CC=gcc-4.7
endif

#CFLAGS=-Wall -ggdb -std=c++0x -O0 -I.
CFLAGS=-Wall -std=c++0x -Os -I.
LDFLAGS= -lmosquittopp -lmosquitto -ljsoncpp -lwbmqtt

MODBUS_BIN=wb-homa-modbus
MODBUS_LIBS=-lmodbus
MODBUS_OBJS=modbus_client.o \
  modbus_config.o modbus_port.o \
  modbus_observer.o \
  uniel.o uniel_context.o
TEST_LIBS=-lgtest -lpthread -lmosquittopp
TEST_DIR=test
TEST_BIN=wb-homa-test

.PHONY: all clean test_fix

all : $(MODBUS_BIN)

# Modbus
main.o : main.cpp
	${CXX} -c $< -o $@ ${CFLAGS}

modbus_client.o : modbus_client.cpp
	${CXX} -c $< -o $@ ${CFLAGS}

modbus_config.o : modbus_config.cpp
	${CXX} -c $< -o $@ ${CFLAGS}

modbus_port.o : modbus_port.cpp
	${CXX} -c $< -o $@ ${CFLAGS}

modbus_observer.o : modbus_observer.cpp
	${CXX} -c $< -o $@ ${CFLAGS}

uniel.o : uniel.cpp
	${CXX} -c $< -o $@ ${CFLAGS}

uniel_context.o : uniel_context.cpp
	${CXX} -c $< -o $@ ${CFLAGS}

$(MODBUS_BIN) : main.o $(MODBUS_OBJS)
	${CXX} $^ ${LDFLAGS} -o $@ $(MODBUS_LIBS)


$(TEST_DIR)/testlog.o: $(TEST_DIR)/testlog.cpp
	${CXX} -c $< -o $@ ${CFLAGS}

$(TEST_DIR)/modbus_test.o: $(TEST_DIR)/modbus_test.cpp
	${CXX} -c $< -o $@ ${CFLAGS}

$(TEST_DIR)/fake_modbus.o: $(TEST_DIR)/fake_modbus.cpp
	${CXX} -c $< -o $@ ${CFLAGS}

$(TEST_DIR)/fake_mqtt.o: $(TEST_DIR)/fake_mqtt.cpp
	${CXX} -c $< -o $@ ${CFLAGS}

$(TEST_DIR)/main.o: $(TEST_DIR)/main.cpp
	${CXX} -c $< -o $@ ${CFLAGS}

$(TEST_DIR)/$(TEST_BIN): $(MODBUS_OBJS) $(COMMON_O) \
  $(TEST_DIR)/testlog.o $(TEST_DIR)/modbus_test.o $(TEST_DIR)/fake_modbus.o \
  $(TEST_DIR)/fake_mqtt.o $(TEST_DIR)/main.o
	${CXX} $^ ${LDFLAGS} -o $@ $(TEST_LIBS) $(MODBUS_LIBS)

test_fix: $(TEST_DIR)/$(TEST_BIN)
	valgrind --error-exitcode=180 -q $(TEST_DIR)/$(TEST_BIN) || \
          if [ $$? = 180 ]; then \
            echo "*** VALGRIND DETECTED ERRORS ***" 1>& 2; \
            exit 1; \
          else $(TEST_DIR)/abt.sh show; exit 1; fi

clean :
	-rm -f *.o $(MODBUS_BIN)
	-rm -f $(TEST_DIR)/*.o $(TEST_DIR)/$(TEST_BIN)



install: all
	install -d $(DESTDIR)
	install -d $(DESTDIR)/etc
	install -d $(DESTDIR)/usr/bin
	install -d $(DESTDIR)/usr/lib
	install -d $(DESTDIR)/usr/share/wb-homa-modbus

	install -m 0644  config.json $(DESTDIR)/etc/wb-homa-modbus.conf.sample
	install -m 0755  $(MODBUS_BIN) $(DESTDIR)/usr/bin/$(MODBUS_BIN)
	cp -r  wb-homa-modbus-templates $(DESTDIR)/usr/share/wb-homa-modbus/templates
