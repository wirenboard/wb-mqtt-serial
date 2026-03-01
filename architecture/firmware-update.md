# Firmware Update Subsystem — Architecture (ARC42)

## 1. Introduction and Goals

### Requirements Overview

Wiren Board RS-485 devices support firmware updates over Modbus. Previously this was handled
by **wb-device-manager** — a Python middleware sitting between the web UI (homeui) and the
serial driver (wb-mqtt-serial). The middleware:

1. Received RPC calls from homeui
2. Downloaded firmware files from the release server
3. Proxied Modbus register reads/writes through wb-mqtt-serial's `port/Load` RPC

This architecture had several problems:
- Extra network hop: homeui → wb-device-manager → wb-mqtt-serial → serial port
- Python process consumed ~50MB RAM on resource-constrained embedded devices
- Two services to maintain, deploy, and debug for one logical operation
- Fragile RPC chaining with no transactional guarantees

### Solution

Move the firmware update logic directly into wb-mqtt-serial, which already has native
Modbus access. This eliminates wb-device-manager entirely. The web UI needs only two
one-line changes to point at the new service.

### Quality Goals

| Priority | Goal | Measure |
|----------|------|---------|
| 1 | Zero-downtime migration | Compat RPC endpoints under old service name |
| 2 | No new heavyweight dependencies | libcurl only; no YAML/Python libs |
| 3 | Testability | >85% line coverage on Modbus task code |
| 4 | Same user experience | Identical JSON response format and MQTT state topic |

### Stakeholders

| Role | Concern |
|------|---------|
| homeui frontend | Stable RPC interface, same JSON schema |
| Device owner | Reliable firmware updates, clear progress/error reporting |
| Maintainer | Single service to debug, no Python dependency |

## 2. Constraints

### Technical Constraints

- **C++17**: wb-mqtt-serial codebase standard
- **libwbmqtt**: MQTT client and RPC server framework (project convention)
- **Debian Bullseye**: Target OS on Wiren Board controllers
- **No YAML library**: The release manifest (`release-versions.yaml`) has a trivial
  3-level structure. A hand-rolled parser avoids adding yaml-cpp (~2MB) as a dependency.
- **libcurl**: Only new dependency. Required for HTTPS downloads from the firmware
  release server. Already widely available on target.

### Organizational Constraints

- Must provide backward-compatible RPC endpoints under `wb-device-manager/fw-update/*`
  and state topic `/wb-device-manager/firmware_update/state` so existing homeui
  installations continue working during the transition period.
- `debian/control` uses `Replaces: wb-device-manager`, `Provides: wb-device-manager`,
  `Conflicts: wb-device-manager` so the package manager handles the takeover.

## 3. Context and Scope

### System Context

```
┌──────────┐   MQTT RPC    ┌──────────────────┐   Modbus/RTU    ┌──────────┐
│  homeui  │──────────────▷│  wb-mqtt-serial   │────────────────▷│ RS-485   │
│  (web)   │◁──────────────│                   │◁────────────────│ devices  │
└──────────┘  state topic   │  ┌──────────────┐│                 └──────────┘
                            │  │ fw-update    ││
                            │  │ subsystem    ││   HTTPS
                            │  └──────┬───────┘│──────────────▷  fw-releases.
                            └─────────┼────────┘                 wirenboard.com
                                      │
                            ┌─────────▽────────┐
                            │  /dev/ttyRS485-*  │
                            └──────────────────┘
```

### RPC Interface (external boundary)

All methods registered under group `fw-update`, accessible via two service names:

| Method | Type | Description |
|--------|------|-------------|
| `GetFirmwareInfo` | async | Read device registers, query release server, return version info |
| `Update` | async | Start firmware/bootloader/component flash, return "Ok" immediately |
| `ClearError` | sync | Remove error state for a device |
| `Restore` | async | Flash firmware on device already in bootloader mode |

**State topic**: `/wb-mqtt-serial/firmware_update/state` (+ compat `/wb-device-manager/...`)

```json
{"devices":[{
  "port":{"path":"/dev/ttyRS485-1"},
  "protocol":"modbus",
  "slave_id":39,
  "to_version":"3.7.0",
  "progress":50,
  "from_version":"3.6.1",
  "type":"firmware",
  "error":null,
  "component_number":null,
  "component_model":null
}]}
```

## 4. Solution Strategy

### Key Decisions

| Decision | Rationale |
|----------|-----------|
| **Direct Modbus access** via `Modbus::MakePDU()` / `IModbusTraits::Transaction()` | Same pattern used by existing `rpc_port_load_modbus_serial_client_task.cpp`. No new abstraction needed. |
| **ISerialClientTask for register I/O** | Reuses the task runner infrastructure. Tasks execute on the serial client thread, which already holds the port lock. No concurrent port access issues. |
| **HTTP in handler thread, Modbus in task thread** | `GetFirmwareInfo` reads registers via task, then does HTTP lookups in the callback (handler thread). Keeps blocking HTTP out of the serial polling loop. |
| **Fire-and-forget flash** | `Update` RPC returns "Ok" after scheduling flash tasks. Progress/errors reported via MQTT state topic. Matches the original Python behavior. |
| **TTL-cached HTTP responses** | Release manifest cached 10min, bootloader info 30min, firmware binaries 2hr. Avoids hammering the release server on repeated queries. |
| **IHttpClient interface** | Abstracts HTTP transport. Production uses libcurl (`TCurlHttpClient`), tests use `TFakeHttpClient` with canned responses. |
| **Second MqttRpcServer for compat** | A separate `TMqttRpcServer` with service name `"wb-device-manager"` registers the same handlers. This makes endpoints available at both `/rpc/v1/wb-mqtt-serial/fw-update/*` and `/rpc/v1/wb-device-manager/fw-update/*`. |

## 5. Building Block View

### Level 1: Subsystem Decomposition

```
src/rpc/
├── rpc_fw_update_handler.h/.cpp   ← RPC method registration & orchestration
├── rpc_fw_update_task.h/.cpp      ← Modbus register I/O (ISerialClientTask)
├── rpc_fw_downloader.h/.cpp       ← HTTP downloads, WBFW parsing, release lookup
└── rpc_fw_update_state.h/.cpp     ← State tracking & MQTT publishing
```

### Level 2: Component Responsibilities

#### TRPCFwUpdateHandler (orchestrator)
- Registers 4 RPC methods on both primary and compat RPC servers
- Parses request parameters (slave_id, port, protocol)
- Coordinates between Modbus tasks and HTTP downloads
- Guards against concurrent updates via `UpdateMutex`

**Cf.** `firmware_update.py:650 FirmwareUpdater`

#### TFwGetInfoTask (ISerialClientTask)
- Reads firmware identity registers: signature, version, bootloader, device model
- Reads component presence bitmap and per-component info
- Handles fallbacks: extended model → standard model, missing bootloader register
- Strips control characters from model string

**Cf.** `firmware_update.py:224 FirmwareInfoReader`

#### TFwFlashTask (ISerialClientTask)
- Optionally reboots device to bootloader (register 131 or 129)
- Writes 32-byte info block to register 0x1000
- Writes data in 136-byte chunks to register 0x2000 with 3-attempt retry
- Reports progress after each chunk via callback

**Cf.** `firmware_update.py:382 flash_fw()`

#### TFwDownloader
- `GetReleasedFirmware(signature, suite)` → version + download URL
- `GetLatestBootloader(signature)` → version + download URL
- `DownloadAndParseWBFW(url)` → info block + data block
- TTL caching for all HTTP responses

**Cf.** `fw_downloader.py:89 get_released_fw()`

#### TFwUpdateState
- Thread-safe list of `TDeviceUpdateInfo` records
- Serializes to JSON and publishes to MQTT on every change
- Mutex released before publishing (avoids holding lock during MQTT I/O)

**Cf.** `firmware_update.py:82 UpdateState`

## 6. Runtime View

### GetFirmwareInfo Sequence

```
homeui                  RPC Handler              Task Runner           Serial Port
  │                        │                        │                      │
  │──GetFirmwareInfo──────▷│                        │                      │
  │                        │──TFwGetInfoTask────────▷│                      │
  │                        │                        │──read registers─────▷│
  │                        │                        │◁─register data───────│
  │                        │◁─callback(TFwDeviceInfo)│                      │
  │                        │                        │                      │
  │                        │──HTTP: get releases───▷ fw-releases server
  │                        │◁─release versions──────
  │                        │                        │                      │
  │◁─JSON response─────────│                        │                      │
```

### Update Sequence

```
homeui                  RPC Handler              Task Runner           Serial Port
  │                        │                        │                      │
  │──Update───────────────▷│                        │                      │
  │                        │──TFwGetInfoTask────────▷│                      │
  │                        │                        │──read signature──────▷│
  │                        │◁─callback(info)────────│                      │
  │                        │                        │                      │
  │                        │──HTTP: download fw────▷ fw-releases server
  │                        │◁─WBFW binary───────────
  │                        │                        │                      │
  │◁─"Ok"─────────────────│                        │                      │
  │                        │──TFwFlashTask──────────▷│                      │
  │                        │  (state: 0%)           │──reboot to BL───────▷│
  │                        │                        │──write info block────▷│
  │                        │                        │──write data chunks──▷│
  │                        │  (state: N%)           │  (progress callback) │
  │                        │  (state: 100%)         │                      │
  │                        │  (state: removed)      │                      │
```

## 7. Deployment View

### Package Takeover

```
debian/control:
  Replaces: wb-device-manager
  Provides: wb-device-manager
  Conflicts: wb-device-manager
```

When `wb-mqtt-serial` is installed, `dpkg` automatically removes `wb-device-manager`.
The `Provides` declaration satisfies any package that depends on `wb-device-manager`.

### Build Dependencies

- `libcurl4-openssl-dev` (build), `libcurl4` (runtime) — added to `debian/control`

## 8. Crosscutting Concepts

### Thread Safety

- `TFwUpdateState::Mutex` protects the device list. JSON is serialized under lock,
  but MQTT publish happens after lock release to avoid holding the mutex during I/O.
- `UpdateMutex` in the handler prevents concurrent Update/Restore operations.
- `CacheMutex` in `TFwDownloader` protects HTTP response caches.
- Flash/GetInfo tasks run on serial client threads; callbacks may fire on those threads.

### Error Handling

- Modbus errors during GetInfo are caught per-register. Missing bootloader version
  or device model gracefully degrades (empty string, fallback register).
- Flash data block writes retry up to 3 times. Modbus exception code 0x04
  (slave device failure) is treated as "already written" (success).
- HTTP errors surface as RPC error responses or state error entries.
- All errors published to the state topic with `com.wb.device_manager.generic_error`
  error ID and exception details in metadata.

### Input Validation

- `slave_id` is required, must be integer in range 0-255
- `port.path` is required and must be non-empty
- Invalid requests return clear error messages via RPC error response

## 9. Architecture Decisions

### ADR-1: No YAML Library

**Context**: The firmware release manifest is a YAML file with 3 nesting levels,
all string values, no special YAML features.

**Decision**: Hand-rolled line-by-line parser (~80 lines).

**Consequences**: +No new dependency (~2MB yaml-cpp). +Fast compile. −Cannot handle
quoted values, anchors, or non-standard indentation. Acceptable because the server-side
format is controlled by the same team.

### ADR-2: Separate RPC Server for Compatibility

**Context**: homeui addresses firmware update RPCs to `wb-device-manager/fw-update/*`.
The MQTT RPC framework derives topic paths from the service name.

**Decision**: Create a second `TMqttRpcServer` with service name `"wb-device-manager"`
and register the same methods on it.

**Alternatives considered**:
- Registering methods under group `wb-device-manager/fw-update` on the main server
  → would create path `/rpc/v1/wb-mqtt-serial/wb-device-manager/fw-update/...` (wrong)
- MQTT topic aliases → not supported by the framework

**Consequences**: +Exact backward compatibility. +Zero changes needed in existing homeui.
−Two RPC servers to start/stop (minor).

### ADR-3: IHttpClient Interface for Testability

**Context**: Firmware downloads use HTTPS. Unit tests cannot make real HTTP requests.

**Decision**: Define `IHttpClient` with `GetText()` and `GetBinary()` methods.
Production uses `TCurlHttpClient` (libcurl). Tests use `TFakeHttpClient` with
pre-configured responses.

**Consequences**: +Full unit test coverage of download/parse/cache logic without
network access. +Easy to mock HTTP errors and edge cases. −One extra level of
indirection (negligible).

## 10. Risks and Technical Debt

| Risk | Mitigation |
|------|------------|
| Handler captures raw `this` in task callbacks | Handler lifetime managed by `shared_ptr` in `main.cpp`; RPC server stopped before handler destruction. Consider `enable_shared_from_this` if lifetime becomes more complex. |
| 500ms `sleep_for` in DoReboot blocks serial polling thread | Acceptable for rare firmware update operations. Document as known limitation. |
| WBFW cache grows unbounded | TTL-based expiry (2hr). In practice, very few distinct firmware URLs are accessed per session. |
| YAML parser is fragile | Format is controlled server-side. Test covers comments and blank lines. Monitor for format changes. |

## 11. Register Map Reference

| Register | Address | Count | Function | Purpose |
|----------|---------|-------|----------|---------|
| fw_signature | 290 | 12 | READ_HOLDING (3) | Device identity for fw lookup |
| fw_version | 250 | 16 | READ_HOLDING (3) | Current firmware version |
| bootloader_version | 330 | 7 | READ_HOLDING (3) | Current bootloader version |
| device_model (ext) | 200 | 20 | READ_HOLDING (3) | Device model string |
| device_model (std) | 200 | 6 | READ_HOLDING (3) | Fallback model string |
| reboot_preserve | 131 | 1 | WRITE_SINGLE (6) | Reboot to BL (new devices) |
| reboot_legacy | 129 | 1 | WRITE_SINGLE (6) | Reboot to BL (old devices) |
| fw_info_block | 0x1000 | 16 | WRITE_MULTIPLE (16) | WBFW header (32 bytes) |
| fw_data_block | 0x2000 | 68 | WRITE_MULTIPLE (16) | Firmware chunk (136 bytes) |
| components_presence | 65152 | 8 | READ_DISCRETE (2) | Component bitmask |
| component_signature | 64788+N*48 | 12 | READ_INPUT (4) | Component N identity |
| component_fw_version | 64800+N*48 | 16 | READ_INPUT (4) | Component N version |
| component_model | 64768+N*48 | 20 | READ_INPUT (4) | Component N model |

## 12. Traceability to Original Python

Every function in the C++ implementation has a comment referencing the corresponding
Python function in [wb-device-manager](https://github.com/wirenboard/wb-device-manager).

| C++ Class/Function | Python Reference |
|---------------------|-----------------|
| `TRPCFwUpdateHandler` | `firmware_update.py:650 FirmwareUpdater` |
| `GetFirmwareInfo()` | `firmware_update.py:682` |
| `Update()` | `firmware_update.py:785` |
| `Restore()` | `firmware_update.py:871` |
| `ClearError()` | `firmware_update.py:850` |
| `StartFlash()` | `firmware_update.py:444 update_software()` |
| `StartComponentsFlash()` | `firmware_update.py:538 update_components()` |
| `BuildFirmwareInfoResponse()` | `firmware_update.py:682` |
| `TFwGetInfoTask::Run()` | `firmware_update.py:311 FirmwareInfoReader.read()` |
| `TFwFlashTask::DoReboot()` | `firmware_update.py:412 reboot_to_bootloader()` |
| `TFwFlashTask::WriteDataBlock()` | `firmware_update.py:349 write_fw_data_block()` |
| `ParseWBFW()` | `firmware_update.py:185 parse_wbfw()` |
| `ParseFwVersionFromUrl()` | `releases.py:30 parse_fw_version()` |
| `GetReleasedFirmware()` | `fw_downloader.py:89 get_released_fw()` |
| `GetLatestBootloader()` | `fw_downloader.py:135 get_latest_bootloader()` |
| `TFwUpdateState` | `firmware_update.py:82 UpdateState` |
| `TUpdateNotifier` | `firmware_update.py:209 UpdateNotifier` |
| `TDeviceUpdateInfo::Matches()` | `firmware_update.py:73 DeviceUpdateInfo.__eq__()` |
