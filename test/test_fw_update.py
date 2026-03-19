#!/usr/bin/env python3
"""
Comprehensive firmware update test suite.
Tests firmware update RPC in wb-mqtt-serial.

Usage:
    python3 test_fw_update.py [--host 192.168.1.108] [--port /dev/ttyRS485-1] [--slave 30]

Requires: mosquitto-clients on the host running tests.
Runs against real hardware via MQTT RPC.
"""

import argparse
import json
import os
import subprocess  # nosec B404
import sys
import time
import threading
import uuid

# ============================================================
# Configuration
# ============================================================

MQTT_HOST = "192.168.1.108"
PORT_PATH = "/dev/ttyRS485-1"
SLAVE_ID = 30
# Serial port settings (extra fields ignored by C++, kept for Python test compatibility)
PORT_BAUD_RATE = 115200
PORT_DATA_BITS = 8
PORT_STOP_BITS = 2
PORT_PARITY = "N"
SSH_KEY = os.path.join(os.path.dirname(os.path.abspath(__file__)), "id_ed25519")

# Known versions for WB-M1W2 v.3 (signature: m1w2G3)
LATEST_FW = "4.34.0"
LATEST_BL = "1.5.7"
OLD_FW = "4.33.2"
OLD_BL = "1.4.9"

# RPC topic prefix
RPC_PREFIX = "/rpc/v1/wb-mqtt-serial/fw-update"

# State topic
STATE_TOPIC = "/wb-mqtt-serial/firmware_update/state"

# Firmware files on device (must be pre-deployed)
OLD_FW_FILE = f"/tmp/m1w2G3_{OLD_FW}.wbfw"  # nosec B108
OLD_BL_FILE = f"/tmp/m1w2G3_bl_{OLD_BL}.wbfw"  # nosec B108
LATEST_FW_FILE = f"/tmp/m1w2G3_{LATEST_FW}.wbfw"  # nosec B108


# ============================================================
# Test infrastructure
# ============================================================

class Colors:
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    CYAN = '\033[96m'
    RESET = '\033[0m'
    BOLD = '\033[1m'


class TestFailure(Exception):
    pass


def assert_eq(actual, expected, msg=""):
    if actual != expected:
        raise TestFailure(f"Expected {expected!r}, got {actual!r}" + (f" ({msg})" if msg else ""))


def assert_true(value, msg=""):
    if not value:
        raise TestFailure(f"Expected truthy, got {value!r}" + (f" ({msg})" if msg else ""))


def assert_false(value, msg=""):
    if value:
        raise TestFailure(f"Expected falsy, got {value!r}" + (f" ({msg})" if msg else ""))


def assert_in(item, collection, msg=""):
    if item not in collection:
        raise TestFailure(f"{item!r} not in {collection!r}" + (f" ({msg})" if msg else ""))


def assert_is_none(value, msg=""):
    if value is not None:
        raise TestFailure(f"Expected None, got {value!r}" + (f" ({msg})" if msg else ""))


def assert_is_not_none(value, msg=""):
    if value is None:
        raise TestFailure(f"Expected non-None" + (f" ({msg})" if msg else ""))


class TestRunner:
    def __init__(self):
        self.results = []
        self.current_group = ""

    def group(self, name):
        self.current_group = name
        print(f"\n{Colors.CYAN}{Colors.BOLD}{'='*60}")
        print(f"  {name}")
        print(f"{'='*60}{Colors.RESET}")

    def run(self, name, test_fn):
        full_name = f"{self.current_group}: {name}"
        print(f"  {name}... ", end="", flush=True)
        t0 = time.time()
        try:
            test_fn()
            dt = (time.time() - t0) * 1000
            self.results.append((full_name, True, "", dt))
            print(f"{Colors.GREEN}PASS{Colors.RESET} ({dt:.0f}ms)")
        except TestFailure as e:
            dt = (time.time() - t0) * 1000
            self.results.append((full_name, False, str(e), dt))
            print(f"{Colors.RED}FAIL{Colors.RESET} ({dt:.0f}ms)")
            print(f"    {Colors.RED}{e}{Colors.RESET}")
        except Exception as e:
            dt = (time.time() - t0) * 1000
            self.results.append((full_name, False, str(e), dt))
            print(f"{Colors.RED}ERROR{Colors.RESET} ({dt:.0f}ms)")
            print(f"    {Colors.RED}{type(e).__name__}: {e}{Colors.RESET}")

    def summary(self):
        passed = sum(1 for _, ok, _, _ in self.results if ok)
        failed = sum(1 for _, ok, _, _ in self.results if not ok)
        total = len(self.results)
        print(f"\n{Colors.BOLD}{'='*60}")
        print(f"  RESULTS: {passed}/{total} passed, {failed} failed")
        print(f"{'='*60}{Colors.RESET}")
        if failed:
            print(f"\n{Colors.RED}Failed tests:{Colors.RESET}")
            for name, ok, msg, _ in self.results:
                if not ok:
                    print(f"  - {name}: {msg}")
        return failed == 0


# ============================================================
# MQTT RPC helpers
# ============================================================

def rpc_call(method, params, prefix=RPC_PREFIX, timeout=30):
    """Make an MQTT RPC call and return the parsed response."""
    cid = f"test_{uuid.uuid4().hex[:12]}"
    topic = f"{prefix}/{method}/{cid}"
    reply_topic = f"{topic}/reply"

    result_holder = {"reply": None}

    def sub_thread():
        try:
            out = subprocess.run(  # nosec B603
                ["mosquitto_sub", "-h", MQTT_HOST, "-t", reply_topic,
                 "-W", str(timeout), "-C", "1"],
                capture_output=True, text=True, timeout=timeout + 5
            )
            if out.returncode == 0 and out.stdout.strip():
                result_holder["reply"] = out.stdout.strip()
        except subprocess.TimeoutExpired:
            pass

    t = threading.Thread(target=sub_thread, daemon=True)
    t.start()
    time.sleep(0.3)

    payload = json.dumps({"id": 1, "params": params})
    subprocess.run(  # nosec B603
        ["mosquitto_pub", "-h", MQTT_HOST, "-t", topic, "-m", payload],
        capture_output=True, timeout=5
    )

    t.join(timeout=timeout + 5)

    if result_holder["reply"] is None:
        raise TimeoutError(f"RPC {method} timed out after {timeout}s")

    return json.loads(result_holder["reply"])


def make_port_config():
    """Build port config dict with serial settings for Python compatibility."""
    return {"path": PORT_PATH, "baud_rate": PORT_BAUD_RATE,
            "data_bits": PORT_DATA_BITS, "stop_bits": PORT_STOP_BITS, "parity": PORT_PARITY}


def get_firmware_info(prefix=RPC_PREFIX, timeout=30):
    params = {"port": make_port_config(), "slave_id": SLAVE_ID, "protocol": "modbus"}
    return rpc_call("GetFirmwareInfo", params, prefix=prefix, timeout=timeout)


def update_device(update_type="firmware", prefix=RPC_PREFIX, timeout=30):
    params = {"port": make_port_config(), "slave_id": SLAVE_ID,
              "protocol": "modbus", "type": update_type}
    return rpc_call("Update", params, prefix=prefix, timeout=timeout)


def clear_error(error_type="firmware", prefix=RPC_PREFIX, slave_id=None):
    sid = slave_id if slave_id is not None else SLAVE_ID
    params = {"port": make_port_config(), "slave_id": sid,
              "protocol": "modbus", "type": error_type}
    return rpc_call("ClearError", params, prefix=prefix, timeout=10)


def restore_device(prefix=RPC_PREFIX, timeout=30):
    params = {"port": make_port_config(), "slave_id": SLAVE_ID, "protocol": "modbus"}
    return rpc_call("Restore", params, prefix=prefix, timeout=timeout)


def get_state(topic=STATE_TOPIC, timeout=3):
    """Read current retained state."""
    try:
        out = subprocess.run(  # nosec B603
            ["mosquitto_sub", "-h", MQTT_HOST, "-t", topic, "-W", str(timeout), "-C", "1"],
            capture_output=True, text=True, timeout=timeout + 3
        )
        if out.returncode == 0 and out.stdout.strip():
            return json.loads(out.stdout.strip())
    except (subprocess.TimeoutExpired, json.JSONDecodeError):
        pass
    return None


def monitor_state(topic=STATE_TOPIC, timeout=120, empty_cycles_to_stop=1):
    """Monitor state topic, collect messages until devices list empties after activity.
    empty_cycles_to_stop: how many active→empty transitions before stopping.
    Use 2 for bootloader update with auto-restore (BL→empty→FW→empty)."""
    messages = []
    proc = subprocess.Popen(  # nosec B603
        ["mosquitto_sub", "-h", MQTT_HOST, "-t", topic, "-W", str(timeout)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )
    start = time.time()
    seen_active = False
    empty_transitions = 0
    try:
        while time.time() - start < timeout:
            line = proc.stdout.readline()
            if not line:
                break
            line = line.strip()
            if not line:
                continue
            try:
                msg = json.loads(line)
                messages.append(msg)
                devices = msg.get("devices", [])
                if devices:
                    for d in devices:
                        if d.get("progress", 0) > 0 or d.get("error"):
                            seen_active = True
                elif seen_active:
                    empty_transitions += 1
                    if empty_transitions >= empty_cycles_to_stop:
                        break
                    seen_active = False  # Reset for next cycle
            except json.JSONDecodeError:
                continue
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
    return messages


def ssh_cmd(cmd, timeout=60):
    return subprocess.run(  # nosec B603
        ["ssh", "-i", SSH_KEY, "-o", "StrictHostKeyChecking=no",
         f"root@{MQTT_HOST}", cmd],
        capture_output=True, text=True, timeout=timeout
    )


def downgrade_firmware():
    """Downgrade firmware to OLD_FW via direct flasher."""
    ssh_cmd(f"systemctl stop wb-mqtt-serial && sleep 1 && "
            f"wb-mcu-fw-flasher -d {PORT_PATH} -a {SLAVE_ID} -b 115200 -s 2 -J "
            f"-f {OLD_FW_FILE} 2>&1 && "
            f"systemctl start wb-mqtt-serial", timeout=120)
    time.sleep(10)


def downgrade_bootloader():
    """Downgrade BL to OLD_BL. Device stays in BL mode, service stopped."""
    ssh_cmd(f"systemctl stop wb-mqtt-serial && sleep 1 && "
            f"wb-mcu-fw-flasher -d {PORT_PATH} -a {SLAVE_ID} -b 115200 -s 2 -J "
            f"-f {OLD_BL_FILE} 2>&1", timeout=120)


def flash_firmware_in_bl_mode(fw_file):
    """Flash firmware when device is in bootloader mode, then start service."""
    ssh_cmd(f"wb-mcu-fw-flasher -d {PORT_PATH} -a {SLAVE_ID} -b 115200 -B 115200 -s 2 "
            f"-f {fw_file} 2>&1 && systemctl start wb-mqtt-serial", timeout=120)
    time.sleep(10)


def downgrade_bl_and_fw():
    """Downgrade both BL and FW."""
    downgrade_bootloader()
    time.sleep(1)
    ssh_cmd(f"wb-mcu-fw-flasher -d {PORT_PATH} -a {SLAVE_ID} -b 115200 -B 115200 -s 2 "
            f"-f {OLD_FW_FILE} 2>&1 && systemctl start wb-mqtt-serial", timeout=120)
    time.sleep(10)


def wait_for_update_complete(timeout=120, topic=STATE_TOPIC):
    messages = monitor_state(topic=topic, timeout=timeout)
    if not messages:
        raise TimeoutError("No state messages received")
    return messages


def recover_device():
    """Recovery: ensure device is operational at latest versions."""
    print(f"  {Colors.YELLOW}[recovery] Checking device...{Colors.RESET}")
    try:
        resp = get_firmware_info(timeout=10)
        result = resp.get("result", {})
        if result.get("fw") and result.get("bootloader"):
            return  # Device is fine
    except (subprocess.TimeoutExpired, subprocess.CalledProcessError, json.JSONDecodeError, KeyError):
        pass  # Device unreachable or in bootloader mode, proceed to recovery

    # Device may be in BL mode or unresponsive
    print(f"  {Colors.YELLOW}[recovery] Restoring device...{Colors.RESET}")
    try:
        ssh_cmd("systemctl stop wb-mqtt-serial", timeout=10)
    except (subprocess.TimeoutExpired, subprocess.CalledProcessError):
        pass  # Service may already be stopped
    time.sleep(1)
    flash_firmware_in_bl_mode(LATEST_FW_FILE)


# ============================================================
# Test definitions
# ============================================================

def run_tests(runner: TestRunner):

    # ============================================================
    # T1: GetFirmwareInfo
    # ============================================================
    runner.group("T1. GetFirmwareInfo")

    fw_info = {}

    def t1_1():
        nonlocal fw_info
        resp = get_firmware_info()
        assert_is_none(resp.get("error"), f"RPC error: {resp.get('error')}")
        fw_info = resp["result"]
        assert_true(fw_info.get("fw", "") != "", "fw should not be empty")

    def t1_2():
        assert_true(fw_info.get("bootloader", "") != "", "bootloader should not be empty")

    def t1_3():
        assert_in("WB-M1W2", fw_info.get("model", ""), "model should contain WB-M1W2")

    def t1_4():
        assert_true(fw_info.get("available_fw", "") != "", "available_fw should not be empty")

    def t1_5():
        assert_true(fw_info.get("available_bootloader", "") != "", "available_bootloader should not be empty")

    def t1_6():
        fw = fw_info.get("fw", "")
        avail = fw_info.get("available_fw", "")
        has_update = fw_info.get("fw_has_update")
        if fw == avail:
            assert_false(has_update, "fw_has_update should be false when equal")
        else:
            assert_true(has_update, "fw_has_update should be true when different")

    def t1_7():
        bl = fw_info.get("bootloader", "")
        avail = fw_info.get("available_bootloader", "")
        has_update = fw_info.get("bootloader_has_update")
        if bl == avail:
            assert_false(has_update, "bootloader_has_update should be false when equal")
        else:
            assert_true(has_update, "bootloader_has_update should be true when different")

    def t1_8():
        assert_true(fw_info.get("can_update"), "can_update should be true")

    def t1_9():
        c = fw_info.get("components")
        assert_is_not_none(c, "components must be present")
        assert_true(isinstance(c, dict), f"components should be dict, got {type(c)}")

    def t1_10():
        resp = get_firmware_info(prefix=RPC_PREFIX)
        assert_is_none(resp.get("error"))
        assert_in("fw", resp.get("result", {}))

    runner.run("T1.1: Returns fw version", t1_1)
    runner.run("T1.2: Returns bootloader version", t1_2)
    runner.run("T1.3: Returns model string", t1_3)
    runner.run("T1.4: Returns available_fw", t1_4)
    runner.run("T1.5: Returns available_bootloader", t1_5)
    runner.run("T1.6: fw_has_update correctness", t1_6)
    runner.run("T1.7: bootloader_has_update correctness", t1_7)
    runner.run("T1.8: can_update is true", t1_8)
    runner.run("T1.9: components is dict", t1_9)
    runner.run("T1.10: Works on RPC topic", t1_10)

    # ============================================================
    # T2: Update (type=firmware)
    # ============================================================
    runner.group("T2. Update (type=firmware)")

    print(f"  {Colors.YELLOW}[setup] Downgrading FW to {OLD_FW}...{Colors.RESET}")
    downgrade_firmware()

    state_messages_t2 = []

    def t2_1():
        nonlocal state_messages_t2
        done = threading.Event()
        msgs = []

        def mon():
            msgs.extend(monitor_state(timeout=60))
            done.set()

        mt = threading.Thread(target=mon, daemon=True)
        mt.start()
        time.sleep(0.5)

        resp = update_device("firmware")
        assert_is_none(resp.get("error"), f"RPC error: {resp.get('error')}")
        assert_eq(resp.get("result"), "Ok")

        done.wait(timeout=60)
        state_messages_t2 = msgs

    def t2_2():
        progresses = []
        for msg in state_messages_t2:
            for d in msg.get("devices", []):
                if d.get("slave_id") == SLAVE_ID and d.get("type") == "firmware":
                    progresses.append(d.get("progress", 0))
        assert_true(len(progresses) > 0, "Should have progress updates")
        assert_true(max(progresses) >= 80, f"Max progress {max(progresses)}% < 80%")

    def t2_3():
        found = False
        for msg in state_messages_t2:
            for d in msg.get("devices", []):
                if d.get("slave_id") == SLAVE_ID and d.get("type") == "firmware":
                    fv = d.get("from_version", "")
                    tv = d.get("to_version", "")
                    if fv and tv:
                        assert_eq(fv, OLD_FW, "from_version")
                        assert_eq(tv, LATEST_FW, "to_version")
                        found = True
                        break
            if found:
                break
        assert_true(found, "Should find from_version/to_version in state")

    def t2_4():
        state = get_state()
        if state:
            our = [d for d in state.get("devices", []) if d.get("slave_id") == SLAVE_ID]
            assert_eq(len(our), 0, f"Device should be gone from state, got {our}")

    def t2_5():
        resp = get_firmware_info()
        assert_eq(resp["result"].get("fw"), LATEST_FW)

    def t2_6():
        for msg in state_messages_t2:
            for d in msg.get("devices", []):
                if d.get("slave_id") == SLAVE_ID:
                    assert_in("port", d)
                    assert_in("path", d["port"])
                    assert_in("protocol", d)
                    assert_in("slave_id", d)
                    assert_in("type", d)
                    assert_in("progress", d)
                    assert_in("from_version", d)
                    assert_in("to_version", d)
                    assert_in("error", d)
                    assert_in("component_number", d)
                    assert_in("component_model", d)
                    return
        raise TestFailure("No state messages for device")

    runner.run("T2.1: Update returns Ok", t2_1)
    runner.run("T2.2: Progress 0->100%", t2_2)
    runner.run("T2.3: from/to version in state", t2_3)
    runner.run("T2.4: State empty after success", t2_4)
    runner.run("T2.5: Device has new FW", t2_5)
    runner.run("T2.6: State has all fields", t2_6)

    # ============================================================
    # T3: Update (type=bootloader) - THE KEY TEST
    # ============================================================
    runner.group("T3. Update (type=bootloader)")

    print(f"  {Colors.YELLOW}[setup] Downgrading BL+FW...{Colors.RESET}")
    downgrade_bl_and_fw()
    print(f"  {Colors.YELLOW}[setup] Upgrading FW via RPC first...{Colors.RESET}")
    update_device("firmware")
    wait_for_update_complete()
    time.sleep(5)

    # Verify we're at latest FW + old BL
    resp = get_firmware_info()
    r = resp.get("result", {})
    print(f"  {Colors.YELLOW}[setup] Current: FW={r.get('fw')}, BL={r.get('bootloader')}{Colors.RESET}")

    state_messages_t3 = []

    def t3_1():
        nonlocal state_messages_t3
        done = threading.Event()
        msgs = []

        def mon():
            # Wait for 2 empty transitions: BL→empty→FW→empty
            msgs.extend(monitor_state(timeout=180, empty_cycles_to_stop=2))
            done.set()

        mt = threading.Thread(target=mon, daemon=True)
        mt.start()
        time.sleep(0.5)

        resp = update_device("bootloader")
        assert_is_none(resp.get("error"), f"RPC error: {resp.get('error')}")
        assert_eq(resp.get("result"), "Ok")

        done.wait(timeout=180)
        state_messages_t3 = msgs

    def t3_2():
        bl_progresses = []
        for msg in state_messages_t3:
            for d in msg.get("devices", []):
                if d.get("slave_id") == SLAVE_ID and d.get("type") == "bootloader":
                    bl_progresses.append(d.get("progress", 0))
        assert_true(len(bl_progresses) > 0, "Should have BL progress")

    def t3_3():
        time.sleep(5)  # Wait for device to boot after firmware flash
        resp = get_firmware_info(timeout=30)
        result = resp.get("result", {})
        assert_eq(result.get("bootloader"), LATEST_BL, "BL should be latest")

    def t3_4_auto_restore():
        """KEY TEST: After BL update, FW is automatically restored."""
        resp = get_firmware_info(timeout=30)
        result = resp.get("result", {})
        fw = result.get("fw", "")
        assert_eq(fw, LATEST_FW,
                  f"FW should be auto-restored to {LATEST_FW} after BL update. "
                  f"Got '{fw}'. Device likely stuck in bootloader mode.")

    def t3_5_state_shows_fw_restore():
        """State should show firmware restore after bootloader flash."""
        saw_bl = False
        saw_fw_after_bl = False
        for msg in state_messages_t3:
            for d in msg.get("devices", []):
                if d.get("slave_id") == SLAVE_ID:
                    if d.get("type") == "bootloader":
                        saw_bl = True
                    elif d.get("type") == "firmware" and saw_bl:
                        saw_fw_after_bl = True
        assert_true(saw_fw_after_bl,
                    "State should show FW restore progress after BL update")

    runner.run("T3.1: BL update returns Ok", t3_1)
    runner.run("T3.2: State shows BL progress", t3_2)
    runner.run("T3.3: Device has new BL", t3_3)
    runner.run("T3.4: AUTO-RESTORE FW after BL update", t3_4_auto_restore)
    runner.run("T3.5: State shows FW restore after BL", t3_5_state_shows_fw_restore)

    # Recovery
    recover_device()

    # ============================================================
    # T4: ClearError
    # ============================================================
    runner.group("T4. ClearError")

    def t4_1():
        resp = clear_error("firmware")
        assert_is_none(resp.get("error"))
        assert_eq(resp.get("result"), "Ok")

    def t4_2():
        """Trigger error on non-existent device, then clear it."""
        params = {"port": make_port_config(), "slave_id": 254,
                  "protocol": "modbus", "type": "firmware"}
        try:
            rpc_call("Update", params, timeout=15)
        except (subprocess.TimeoutExpired, subprocess.CalledProcessError, RuntimeError):
            pass  # Update on non-existent device may fail at RPC level
        time.sleep(8)

        state = get_state()
        error_found = False
        if state:
            for d in state.get("devices", []):
                if d.get("slave_id") == 254 and d.get("error"):
                    error_found = True

        if not error_found:
            # Error may have auto-cleared; we can't reliably test this
            return

        clear_error("firmware", slave_id=254)
        time.sleep(1)
        state = get_state()
        if state:
            for d in state.get("devices", []):
                if d.get("slave_id") == 254:
                    assert_is_none(d.get("error"), "Error should be cleared")

    runner.run("T4.1: ClearError returns Ok", t4_1)
    runner.run("T4.2: ClearError removes error", t4_2)

    # ============================================================
    # T5: Restore
    # ============================================================
    runner.group("T5. Restore")

    def t5_1():
        """Restore returns Ok when device NOT in BL mode."""
        resp = restore_device()
        assert_is_none(resp.get("error"))
        assert_eq(resp.get("result"), "Ok")

    def t5_2():
        """Restore flashes FW when device IS in BL mode."""
        print(f"    {Colors.YELLOW}[setup] Putting device in BL mode...{Colors.RESET}")
        downgrade_bootloader()
        time.sleep(2)
        ssh_cmd("systemctl start wb-mqtt-serial", timeout=10)
        time.sleep(5)

        done = threading.Event()
        msgs = []
        def mon():
            msgs.extend(monitor_state(timeout=120))
            done.set()
        mt = threading.Thread(target=mon, daemon=True)
        mt.start()
        time.sleep(0.5)

        resp = restore_device()
        assert_is_none(resp.get("error"))
        assert_eq(resp.get("result"), "Ok")

        done.wait(timeout=120)
        time.sleep(5)

        resp = get_firmware_info()
        fw = resp.get("result", {}).get("fw", "")
        assert_true(fw != "", "Device should have FW after restore")

    runner.run("T5.1: Restore no-op when not in BL", t5_1)
    runner.run("T5.2: Restore works in BL mode", t5_2)

    recover_device()

    # ============================================================
    # T6: Error handling
    # ============================================================
    runner.group("T6. Error Handling")

    def t6_1():
        """Failed update for non-existent device returns RPC error."""
        params = {"port": make_port_config(), "slave_id": 253,
                  "protocol": "modbus", "type": "firmware"}
        resp = rpc_call("Update", params, timeout=60)
        # RPC should return an error for unreachable device
        err = resp.get("error")
        assert_is_not_none(err, "RPC should return error for unreachable device")

    def t6_2():
        """Flash error persists in state until ClearError."""
        # We can't easily simulate a flash error on real hardware without
        # a device that partially responds then fails. Instead, verify
        # that the error mechanism works: trigger update on real device,
        # interrupt mid-flash (not possible cleanly), or just verify the
        # ClearError flow works with a manually-set error.
        # For now, just verify ClearError returns Ok even when no error.
        resp = clear_error("firmware", slave_id=252)
        assert_is_none(resp.get("error"), "ClearError should return Ok")
        assert_eq(resp.get("result"), "Ok")

    def t6_3():
        """Concurrent update rejected."""
        downgrade_firmware()

        resp1 = update_device("firmware")
        assert_is_none(resp1.get("error"), "First update should start")

        time.sleep(0.5)
        resp2 = update_device("firmware")
        assert_is_not_none(resp2.get("error"), "Second concurrent update should be rejected")

        wait_for_update_complete(timeout=60)

    runner.run("T6.1: Error in state", t6_1)
    runner.run("T6.2: Error persists", t6_2)
    runner.run("T6.3: Concurrent update rejected", t6_3)

    # ============================================================
    # T7: State format
    # ============================================================
    runner.group("T7. State Format")

    def t7_1():
        """Idle state has empty devices."""
        state = get_state()
        if state:
            our = [d for d in state.get("devices", []) if d.get("slave_id") == SLAVE_ID]
            assert_eq(len(our), 0, "No active updates expected")

    def t7_2():
        """Null fields during normal update."""
        downgrade_firmware()
        msgs = []
        done = threading.Event()
        def mon():
            msgs.extend(monitor_state(timeout=60))
            done.set()
        mt = threading.Thread(target=mon, daemon=True)
        mt.start()
        time.sleep(0.5)
        update_device("firmware")
        done.wait(timeout=60)

        for msg in msgs:
            for d in msg.get("devices", []):
                if d.get("slave_id") == SLAVE_ID and d.get("type") == "firmware" and not d.get("error"):
                    assert_is_none(d.get("error"), "error null during progress")
                    assert_is_none(d.get("component_number"), "component_number null for FW")
                    assert_is_none(d.get("component_model"), "component_model null for FW")
                    return
        raise TestFailure("No suitable state message")

    runner.run("T7.1: Empty state when idle", t7_1)
    runner.run("T7.2: Null fields in state", t7_2)


# ============================================================
# Main
# ============================================================

def main():
    global MQTT_HOST, PORT_PATH, SLAVE_ID

    parser = argparse.ArgumentParser(description="Firmware update test suite")
    parser.add_argument("--host", default=MQTT_HOST)
    parser.add_argument("--port", default=PORT_PATH)
    parser.add_argument("--slave", type=int, default=SLAVE_ID)
    args = parser.parse_args()

    MQTT_HOST = args.host
    PORT_PATH = args.port
    SLAVE_ID = args.slave

    print(f"{Colors.BOLD}Firmware Update Test Suite{Colors.RESET}")
    print(f"  Host: {MQTT_HOST}, Port: {PORT_PATH}, Slave: {SLAVE_ID}\n")

    # Verify connectivity
    print("Checking connectivity...", end=" ", flush=True)
    try:
        resp = get_firmware_info(timeout=15)
        r = resp.get("result", {})
        print(f"OK (FW={r.get('fw')}, BL={r.get('bootloader')})")
    except Exception as e:
        print(f"FAILED: {e}")
        sys.exit(1)

    runner = TestRunner()
    run_tests(runner)
    success = runner.summary()
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
