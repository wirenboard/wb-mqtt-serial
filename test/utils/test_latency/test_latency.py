#!/usr/bin/env python3

import argparse
import json
import random
import subprocess
import sys
import threading
import time
from collections import defaultdict
from pathlib import Path

import gpiod
from wb_common.mqtt_client import MQTTClient

APP_NAME = "test-latency"
BUTTON_LOCK = 1
BUTTON_UNLOCK = 0
NOT_INITIALIZED = -1


def ns_to_ms(value: int) -> int:
    return round(value / 1000000)


def ms_to_ns(value: int) -> int:
    return value * 1000000


def ms_to_s(value: int) -> int:
    return value / 1000

# https://stackoverflow.com/questions/3173320/text-progress-bar-in-terminal-with-block-characters
def printProgressBar (iteration, total, prefix = '', suffix = '', decimals = 1, length = 100, fill = '█', printEnd = "\r"):
    """
    Call in a loop to create terminal progress bar
    @params:
        iteration   - Required  : current iteration (Int)
        total       - Required  : total iterations (Int)
        prefix      - Optional  : prefix string (Str)
        suffix      - Optional  : suffix string (Str)
        decimals    - Optional  : positive number of decimals in percent complete (Int)
        length      - Optional  : character length of bar (Int)
        fill        - Optional  : bar fill character (Str)
        printEnd    - Optional  : end character (e.g. "\r", "\r\n") (Str)
    """
    percent = ("{0:." + str(decimals) + "f}").format(100 * (iteration / float(total)))
    filledLength = int(length * iteration // total)
    bar = fill * filledLength + '-' * (length - filledLength)
    print(f'\r{prefix} |{bar}| {percent}% {suffix}', end = printEnd)
    # Print New Line on Complete
    if iteration == total: 
        print()

class Line:
    def __init__(self, line_name: str, latency, single_click_time_ms: int, single_click_timeout_ms: int):
        self.line = gpiod.find_line(line_name)
        if self.line is None:
            sys.exit(1)
        self.line.request(consumer=APP_NAME, type=gpiod.LINE_REQ_DIR_OUT)
        self.line.set_value(BUTTON_UNLOCK)
        self.counters = defaultdict(lambda: NOT_INITIALIZED)
        self.latency = latency
        self.single_click_timeout_ms = single_click_timeout_ms
        self.single_click_time_ms = single_click_time_ms
        self.click_time_ns = 0

    def single_click(self) -> None:
        for topic, counter in self.counters.items():
            if counter != -1:
                self.counters[topic] += 1
        self.line.set_value(BUTTON_LOCK)
        time.sleep(ms_to_s(self.single_click_time_ms))
        self.click_time_ns = time.monotonic_ns()
        self.line.set_value(BUTTON_UNLOCK)
        time.sleep(ms_to_s(self.single_click_timeout_ms))

    def reaction(self, topic: str, value: str, reaction_time_ns: int) -> bool:
        int_value = int(value)
        if self.counters[topic] == NOT_INITIALIZED:
            self.counters[topic] = int_value
            return True
        if self.counters[topic] <= int_value:
            delta = reaction_time_ns - self.click_time_ns - ms_to_ns(self.single_click_timeout_ms)
            self.latency.add(topic, delta)
            self.counters[topic] = int_value
            return True
        print("{}: wait {} got {}".format(topic, self.counters[topic], int_value))
        return False


def print_hist(hist, hist_interval_ms: int) -> None:
    for item in hist:
        print("{} - {}: {}".format(item[0], item[0] + hist_interval_ms, item[1]))


def store_raw_latencies(latencies, path: str) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as file:
        file.write("latency (ns), count\n")
        for item in latencies:
            file.write("{},{}\n".format(item[0], item[1]))


def store_hist(hist, path: str) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as file:
        file.write("latency (ms), count\n")
        for item in hist:
            file.write("{},{}\n".format(item[0], item[1]))


def get_converted_latencies(latencies):
    return sorted(latencies.items(), key=lambda a: a[0])


def make_hist(latencies, hist_interval_ms: int):
    items = get_converted_latencies(latencies)
    hist_interval_ns = ms_to_ns(hist_interval_ms)
    start = (items[0][0] // hist_interval_ns) * hist_interval_ns
    hist = []
    count = 0
    for item in items:
        while item[0] >= start + hist_interval_ns:
            hist.append([ns_to_ms(start), count])
            count = 0
            start += hist_interval_ns
        count += 1
    hist.append([ns_to_ms(start), count])
    return hist


class Iteration:
    def __init__(self):
        self.lock = threading.Lock()
        self.event = threading.Event()
        self.accepted_count = 0
        self.count = 0

    def start(self):
        with self.lock:
            self.accepted_count = 0
        self.event.clear()

    def add_reaction(self):
        with self.lock:
            self.accepted_count += 1
        if self.accepted_count == self.count:
            self.event.set()

    def wait(self):
        self.event.wait()

    def set_count(self, count):
        self.count = count


class MqttWrapper:
    def __init__(self):
        self.client = MQTTClient(APP_NAME)
        self.client.on_message = lambda client, userdata, msg: self.process_mqtt_message(msg)
        self.client.start()
        self.handler = None
        self.subscriptions = []

    def process_mqtt_message(self, msg):
        msg_time_ns = time.monotonic_ns()
        if self.handler is not None:
            self.handler(msg, msg_time_ns)

    def set_handler(self, handler):
        self.handler = handler

    def subscribe(self, topic: str):
        self.client.subscribe(topic)
        self.subscriptions.append(topic)

    def unsubscribe(self):
        for topic in self.subscriptions:
            self.client.unsubscribe(topic)
        self.subscriptions = []


class LatencyAccumulator:
    def __init__(self):
        self.data = defaultdict(lambda: defaultdict(int))

    def clear(self):
        self.data.clear()

    def add(self, topic: str, latency_ns: int):
        self.data[topic][latency_ns] += 1

    def get_general_latencies(self):
        res = defaultdict(int)
        for _topic, latencies in self.data.items():
            for latency, count in latencies.items():
                res[latency] += count
        return res


def process_mqtt_message(msg, control_to_line, iteration: Iteration, msg_time_ns: int):
    if len(msg.payload) and control_to_line[msg.topic].reaction(
        msg.topic, msg.payload.decode("utf-8"), msg_time_ns
    ):
        iteration.add_reaction()


def get_file_name_from_topic(topic: str) -> str:
    parts = topic.split("/")
    return "{}_{}".format(parts[2], parts[4])


def print_and_store(folder: str, test_name: str, latency: LatencyAccumulator, hist_interval_ms: int) -> None:
    general_latencies = latency.get_general_latencies()
    store_raw_latencies(
        get_converted_latencies(general_latencies), "{}/{}/general_raw.csv".format(folder, test_name)
    )
    hist = make_hist(general_latencies, hist_interval_ms)
    print_hist(hist, hist_interval_ms)
    store_hist(hist, "{}/{}/general.csv".format(folder, test_name))
    for topic, latencies in latency.data.items():
        store_hist(
            make_hist(latencies, hist_interval_ms),
            "{}/{}/{}.csv".format(folder, test_name, get_file_name_from_topic(topic)),
        )


def run_test(test_config, mqtt: MqttWrapper, folder: str):
    print(
        "{}\n  max cycle timeout: {} ms\n  wb-mqtt-serial config: {}".format(
            test_config["name"],
            str(test_config["max-cycle-timeout-ms"]),
            test_config["wb-mqtt-serial-config"],
        )
    )

    serial = subprocess.Popen(["wb-mqtt-serial", "-c", test_config["wb-mqtt-serial-config"]])

    latency = LatencyAccumulator()
    lines = []
    control_to_line = {}
    iteration = Iteration()

    for line_name, controls in test_config["lines"].items():
        line = Line(
            line_name,
            latency,
            int(test_config["single-click-time-ms"]),
            int(test_config["single-click-timeout-ms"]),
        )
        lines.append(line)
        for topic in controls:
            control_to_line[topic] = line

    iteration.set_count(len(control_to_line))
    iteration.start()

    mqtt.set_handler(
        lambda msg, msg_time_ns: process_mqtt_message(msg, control_to_line, iteration, msg_time_ns)
    )

    for topic in control_to_line:
        mqtt.subscribe(topic)

    # Wait for controls creation
    iteration.wait()

    # Skip first click as it can include other controls creation time
    iteration.start()
    for line in lines:
        line.single_click()
    iteration.wait()

    # It is now allowed to test latency
    latency.clear()

    iterations = test_config["iterations"]
    for _n in range(iterations):
        printProgressBar(_n, iterations)
        timeout = ms_to_s(random.SystemRandom().randint(0, test_config["max-cycle-timeout-ms"]))
        time.sleep(timeout)
        iteration.start()
        for line in lines:
            line.single_click()
        iteration.wait()

    printProgressBar(iterations, iterations)
    mqtt.set_handler(None)
    mqtt.unsubscribe()
    serial.terminate()
    serial.wait()

    print_and_store(folder, test_config["name"], latency, int(test_config["hist-interval-ms"]))


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Latency tester", formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    parser.add_argument("-c", "--config", type=str, default="config.json", help="Config file")
    args = parser.parse_args()

    with open(args.config, "r", encoding="utf-8") as f:
        config = json.load(f)

    subprocess.run(["systemctl", "stop", "wb-mqtt-gpio"])
    subprocess.run(["systemctl", "stop", "wb-mqtt-serial"])

    mqtt = MqttWrapper()

    folder = time.strftime("%Y_%m_%d_%H_%M_%S")
    for test_config in config["tests"]:
        if test_config.get("enable", True):
            run_test(test_config, mqtt, folder)


main()
