CC = emcc

SRC_DIR = src
WBLIB_DIR = wblib
DST_DIR = wasm

JSONCPP_DIR = $(WBLIB_DIR)/thirdparty/valijson/thirdparty/jsoncpp-1.9.4
JSONCPP_SRC = $(JSONCPP_DIR)/src/lib_json

VALIJSON_DIR = $(WBLIB_DIR)/thirdparty/valijson

INC = \
	.                      \
	$(SRC_DIR)             \
	$(JSONCPP_DIR)/include  \
	$(VALIJSON_DIR)/include \

SRC = \
	$(SRC_DIR)/bcd_utils.cpp                            \
	$(SRC_DIR)/bin_utils.cpp                            \
	$(SRC_DIR)/crc16.cpp                                \
	$(SRC_DIR)/common_utils.cpp                         \
	$(SRC_DIR)/config_merge_template.cpp                \
	$(SRC_DIR)/expression_evaluator.cpp                 \
	$(SRC_DIR)/file_utils.cpp                           \
	$(SRC_DIR)/json_common.cpp                          \
	$(SRC_DIR)/log.cpp                                  \
	$(SRC_DIR)/modbus_base.cpp                          \
	$(SRC_DIR)/modbus_ext_common.cpp                    \
	$(SRC_DIR)/modbus_common.cpp                        \
	$(SRC_DIR)/pollable_device.cpp                      \
	$(SRC_DIR)/register.cpp                             \
	$(SRC_DIR)/register_value.cpp                       \
	$(SRC_DIR)/register_handler.cpp                     \
	$(SRC_DIR)/serial_client.cpp                        \
	$(SRC_DIR)/serial_client_device_access_handler.cpp  \
	$(SRC_DIR)/serial_client_events_reader.cpp          \
	$(SRC_DIR)/serial_client_register_poller.cpp        \
	$(SRC_DIR)/serial_config.cpp                        \
	$(SRC_DIR)/serial_device.cpp                        \
	$(SRC_DIR)/serial_exc.cpp                           \
	$(SRC_DIR)/templates_map.cpp                        \
	$(SRC_DIR)/wasm.cpp                                 \
	$(SRC_DIR)/wb_registers.cpp                         \
	$(SRC_DIR)/write_channel_serial_client_task.cpp     \
	$(SRC_DIR)/devices/modbus_device.cpp                \
	$(SRC_DIR)/port/port.cpp                            \
	$(SRC_DIR)/port/feature_port.cpp                    \
	$(SRC_DIR)/port/wasm_port.cpp                       \
	$(SRC_DIR)/rpc/rpc_config.cpp                       \
	$(SRC_DIR)/rpc/rpc_device_handler.cpp               \
	$(SRC_DIR)/rpc/rpc_device_load_task.cpp             \
	$(SRC_DIR)/rpc/rpc_device_load_config_task.cpp      \
	$(SRC_DIR)/rpc/rpc_device_set_task.cpp              \
	$(SRC_DIR)/rpc/rpc_device_probe_task.cpp            \
	$(SRC_DIR)/rpc/rpc_exception.cpp                    \
	$(SRC_DIR)/rpc/rpc_helpers.cpp                      \
	$(SRC_DIR)/rpc/rpc_port_scan_serial_client_task.cpp \
	$(WBLIB_DIR)/base_lock_object.cpp                   \
	$(WBLIB_DIR)/exceptions.cpp                         \
	$(WBLIB_DIR)/json_utils.cpp                         \
	$(WBLIB_DIR)/log.cpp                                \
	$(WBLIB_DIR)/thread_utils.cpp                       \
	$(WBLIB_DIR)/utils.cpp                              \
	$(JSONCPP_SRC)/json_reader.cpp                      \
	$(JSONCPP_SRC)/json_value.cpp                       \
	$(JSONCPP_SRC)/json_writer.cpp                      \

FILE = \
	wb-mqtt-serial-confed-common.schema.json                              \
	wb-mqtt-serial-device-template.schema.json                            \
	wasm/schema/wb-mqtt-serial-rpc-device-load-config-request.schema.json \
	wasm/schema/wb-mqtt-serial-rpc-device-set-request.schema.json         \
	wasm/schema/wb-mqtt-serial-rpc-port-scan-request.schema.json          \
	wasm/templates                                                        \

OPT = \
	-fexceptions                                    \
	-lembind                                        \
	-sASYNCIFY                                      \
	-sASYNCIFY_IMPORTS=["emscripten_asm_const_int"] \

all:
# copy templates
	mkdir -p $(DST_DIR)/templates
	cp templates/config-map*.json $(DST_DIR)/templates
	cp templates/config-wb-*.json $(DST_DIR)/templates
	cp build/templates/config-map*.json $(DST_DIR)/templates
	cp build/templates/config-wb-*.json $(DST_DIR)/templates
# fix include
	cp -r $(JSONCPP_DIR)/include/json wblib/
# build module
	$(CC) -v -O3 $(addprefix -I, $(INC)) $(SRC) -o $(DST_DIR)/module.js $(addprefix --preload-file , $(FILE)) $(OPT)
