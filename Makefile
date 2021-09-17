ifneq ($(DEB_HOST_MULTIARCH),)
	CROSS_COMPILE ?= $(DEB_HOST_MULTIARCH)-
endif

ifeq ($(origin CC),default)
	CC := $(CROSS_COMPILE)gcc
endif
ifeq ($(origin CXX),default)
	CXX := $(CROSS_COMPILE)g++
endif

ifeq ($(DEBUG),)
	BUILD_DIR ?= build/release
else
	BUILD_DIR ?= build/debug
endif

# extract Git revision and version number from debian/changelog
GIT_REVISION:=$(shell git rev-parse HEAD)
DEB_VERSION:=$(shell head -1 debian/changelog | awk '{ print $$2 }' | sed 's/[\(\)]//g')

SERIAL_BIN = wb-mqtt-serial
SRC_DIR = src

GURUX_SRC = thirdparty/gurux/development/src
GURUX_INCLUDE = thirdparty/gurux/development/include

COMMON_SRCS := $(shell find $(SRC_DIR) $(GURUX_SRC) \( -name *.cpp -or -name *.c \) -and -not -name main.cpp)
COMMON_OBJS := $(COMMON_SRCS:%=$(BUILD_DIR)/%.o)

LDFLAGS = -lpthread -lwbmqtt1
CXXFLAGS = -std=c++14 -Wall -Werror -I$(SRC_DIR) -I$(GURUX_INCLUDE) -DWBMQTT_COMMIT="$(GIT_REVISION)" -DWBMQTT_VERSION="$(DEB_VERSION)" -Wno-psabi
CFLAGS = -Wall -I$(SRC_DIR) -I$(GURUX_INCLUDE)

ifeq ($(DEBUG),)
	CXXFLAGS += -O3
else
	CXXFLAGS += -g -O0 -fprofile-arcs -ftest-coverage -ggdb
	LDFLAGS += -lgcov
endif

TEST_DIR = test
TEST_SRCS := $(shell find $(TEST_DIR) \( -name *.cpp -or -name *.c \))
TEST_OBJS := $(TEST_SRCS:%=$(BUILD_DIR)/%.o)
TEST_BIN = wb-homa-test
TEST_LDFLAGS = -lgtest -lwbmqtt_test_utils

SRCS=$(SERIAL_SRCS) $(TEST_SRCS)

.PHONY: all clean test

all : $(SERIAL_BIN)

$(SERIAL_BIN): $(COMMON_OBJS) $(BUILD_DIR)/src/main.cpp.o
	${CXX} -o $(BUILD_DIR)/$@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.c.o: %.c
	${CC} -c $< -o $@ ${CFLAGS}

$(BUILD_DIR)/%.cpp.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) -c $(CXXFLAGS) -o $@ $^

$(BUILD_DIR)/test/%.o: test/%.cpp
	$(CXX) -c $(CXXFLAGS) -o $@ $^

$(TEST_DIR)/$(TEST_BIN): $(COMMON_OBJS) $(TEST_OBJS)
	${CXX} $^ ${LDFLAGS} $(TEST_LDFLAGS) -o $@ -fno-lto

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
	rm -rf build/release
	rm -rf build/debug
	rm -rf $(TEST_DIR)/*.o $(TEST_DIR)/$(TEST_BIN)
	find $(SRC_DIR) -name '*.o' -delete
	rm -f $(SERIAL_BIN)

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

	install -D -m 0644  obis-hints.json $(DESTDIR)/usr/share/wb-mqtt-serial/obis-hints.json

	install -D -m 0755  $(BUILD_DIR)/$(SERIAL_BIN) $(DESTDIR)/usr/bin/$(SERIAL_BIN)
