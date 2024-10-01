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

GENERATED_TEMPLATES_DIR = build/templates

PREFIX = /usr

# extract Git revision and version number from debian/changelog
GIT_REVISION:=$(shell git rev-parse HEAD)
DEB_VERSION:=$(shell head -1 debian/changelog | awk '{ print $$2 }' | sed 's/[\(\)]//g')

SERIAL_BIN = wb-mqtt-serial
SRC_DIR = src

GURUX_SRC = thirdparty/gurux/development/src
GURUX_INCLUDE = thirdparty/gurux/development/include

COMMON_SRCS := $(shell find $(SRC_DIR) $(GURUX_SRC) \( -name "*.cpp" -or -name "*.c" \) -and -not -name main.cpp)
COMMON_OBJS := $(COMMON_SRCS:%=$(BUILD_DIR)/%.o)

LDFLAGS = -lpthread -lwbmqtt1 -lstdc++fs
CXXFLAGS = -std=c++17 -Wall -Werror -I$(SRC_DIR) -I$(GURUX_INCLUDE) -DWBMQTT_COMMIT="$(GIT_REVISION)" -DWBMQTT_VERSION="$(DEB_VERSION)" -Wno-psabi

ifeq ($(DEBUG),)
	CXXFLAGS += -O3 -DNDEBUG
else
	CXXFLAGS += -g -O0 --coverage -ggdb
	LDFLAGS += --coverage
endif

TEST_DIR = test
TEST_SRCS := $(shell find $(TEST_DIR) \( -name "*.cpp" -or -name "*.c" \))
TEST_OBJS := $(TEST_SRCS:%=$(BUILD_DIR)/%.o)
TEST_BIN = wb-homa-test
TEST_LDFLAGS = -lgtest -lwbmqtt_test_utils

VALGRIND_FLAGS = --error-exitcode=180 -q

COV_REPORT ?= $(BUILD_DIR)/cov.html
GCOVR_FLAGS := -s --html $(COV_REPORT)
ifneq ($(COV_FAIL_UNDER),)
	GCOVR_FLAGS += --fail-under-line $(COV_FAIL_UNDER)
endif

SRCS=$(SERIAL_SRCS) $(TEST_SRCS)

TEMPLATES_DIR = templates
JINJA_TEMPLATES = $(wildcard $(TEMPLATES_DIR)/*.json.jinja)

.PHONY: all clean test templates

all : templates $(SERIAL_BIN)

$(SERIAL_BIN): $(COMMON_OBJS) $(BUILD_DIR)/$(SRC_DIR)/main.cpp.o
	$(CXX) -o $(BUILD_DIR)/$@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.cpp.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) -c $(CXXFLAGS) -o $@ $^

$(TEST_DIR)/$(TEST_BIN): $(COMMON_OBJS) $(TEST_OBJS)
	$(CXX) $^ $(LDFLAGS) $(TEST_LDFLAGS) -o $@ -fno-lto

$(GENERATED_TEMPLATES_DIR)/%.json: $(TEMPLATES_DIR)/%.json.jinja
	mkdir -p $(GENERATED_TEMPLATES_DIR)
	(cd $(TEMPLATES_DIR); j2 -o ../$@ $(notdir $^))

test: templates $(TEST_DIR)/$(TEST_BIN)
	rm -f $(TEST_DIR)/*.dat.out
	if [ "$(shell arch)" != "armv7l" ] && [ "$(CROSS_COMPILE)" = "" ] || [ "$(CROSS_COMPILE)" = "x86_64-linux-gnu-" ]; then \
		valgrind $(VALGRIND_FLAGS) $(TEST_DIR)/$(TEST_BIN) $(TEST_ARGS) || \
		if [ $$? = 180 ]; then \
			echo "*** VALGRIND DETECTED ERRORS ***" 1>& 2; \
			exit 1; \
		else $(TEST_DIR)/abt.sh show; exit 1; fi; \
	else \
		$(TEST_DIR)/$(TEST_BIN) $(TEST_ARGS) || { $(TEST_DIR)/abt.sh show; exit 1; } \
	fi
ifneq ($(DEBUG),)
	gcovr $(GCOVR_FLAGS) $(BUILD_DIR)/$(SRC_DIR) $(BUILD_DIR)/$(TEST_DIR)
endif

templates: $(JINJA_TEMPLATES:$(TEMPLATES_DIR)/%.json.jinja=$(GENERATED_TEMPLATES_DIR)/%.json)

clean:
	-rm -rf build
	-rm -rf $(TEST_DIR)/$(TEST_BIN)

install:
	install -d $(DESTDIR)/var/lib/wb-mqtt-serial
	install -d $(DESTDIR)/etc/wb-mqtt-serial.conf.d/templates

	install -Dm0644 config.sample.json $(DESTDIR)/etc/wb-mqtt-serial.conf.sample

	for cfg in configs/config.json.*; do \
	    board=$${cfg##*.}; \
	    install -Dm0644 $$cfg $(DESTDIR)$(PREFIX)/share/wb-mqtt-serial/wb-mqtt-serial.conf.$$board; \
	done

	install -Dm0644 wb-mqtt-serial.wbconfigs $(DESTDIR)/etc/wb-configs.d/11wb-mqtt-serial

	install -Dm0644 *.schema.json -t $(DESTDIR)$(PREFIX)/share/wb-mqtt-serial
	install -Dm0644 groups.json -t $(DESTDIR)$(PREFIX)/share/wb-mqtt-serial
	install -Dm0644 templates/*.json $(GENERATED_TEMPLATES_DIR)/*.json -t $(DESTDIR)$(PREFIX)/share/wb-mqtt-serial/templates
	install -Dm0644 protocols/*.json -t $(DESTDIR)$(PREFIX)/share/wb-mqtt-serial/protocols

	install -Dm0644 obis-hints.json -t $(DESTDIR)$(PREFIX)/share/wb-mqtt-serial

	install -Dm0644 wb-mqtt-serial-dummy.schema.json -t $(DESTDIR)$(PREFIX)/share/wb-mqtt-confed/schemas

	install -Dm0755 $(BUILD_DIR)/$(SERIAL_BIN) -t $(DESTDIR)$(PREFIX)/bin
