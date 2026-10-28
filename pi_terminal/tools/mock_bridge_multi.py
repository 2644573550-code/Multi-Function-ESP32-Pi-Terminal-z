#!/usr/bin/env python3
"""PBR1 USB/UART 模拟器：桥板 + 双槽 + 电池遥测。

比单模块版多三件事：
  1. 双槽：同一桥板管理 slot 0/1，各自可插拔独立模块
  2. 电池：周期性上报电量（可选）
  3. 刷卡：模拟 RC522 的 rfid_card 事件

用法（演示脚本会用）：
    bridge = MultiSlotBridge()
    bridge.attach(0, module=80, instance='planter-a')
    bridge.attach(1, module=82, instance='fence-b')
    bridge.set_battery(percent=82, charging=False)
    bridge.present_card('a1b2c3d4')
"""
import binascii
import json
import os
import pty
import struct
import threading
import time

MAGIC = b'PBR1'
# PiBridge::Kind
KIND_CONTROL_EVENT = 1


def frame(payload: dict, sequence: int) -> bytes:
    """PBR1 帧：MAGIC | dir | kind | seq_u32le | len_u16le | payload | crc16"""
    body = struct.pack('<BBIH', 1, KIND_CONTROL_EVENT, sequence, len(
        json.dumps(payload, separators=(',', ':')).encode())) + \
        json.dumps(payload, separators=(',', ':')).encode()
    return MAGIC + body + struct.pack('<H', binascii.crc_hqx(body, 0xffff))


class MultiSlotBridge:
    """单桥板 + 双槽。每个槽可以独立挂/拔模块。"""

    def __init__(self):
        self.master, self.slave = pty.openpty()
        self.path = os.ttyname(self.slave)
        self.sequence = 0
        self.lock = threading.Lock()
        self.timers = []
        self.slots = {}          # slot_id -> {'module': int, 'instance': str}
        self.battery = None      # None = 无遥测（Pi 侧显示未知）

    # ---- 底层 ----
    def _emit(self, payload: dict):
        with self.lock:
            self.sequence += 1
            try:
                os.write(self.master, frame(payload, self.sequence))
            except OSError:
                pass   # 宿主管道已关

    # ---- 桥板/槽位事件 ----
    def attach(self, slot_id: int, module: int, instance: str, delay: float = 0.0):
        """把一个模块插到指定槽。带 slot_id/device_id/binding_generation 四元组。"""
        self.slots[slot_id] = {'module': module, 'instance': instance}

        def do():
            self._emit({
                'event': 'attach',
                'module_id': module,
                'module_instance': instance,
                'slot_id': slot_id,
                'device_id': instance,
                'binding_generation': '%032x' % (slot_id + 1),
                'bridge_id': 'bridge-demo',
                'module_config': {
                    'schema': 'module-transport-v1',
                    'transport_mode': 'fixed_hotspot_lan',
                    'package_transport': 'data',
                },
            })

        if delay > 0:
            t = threading.Timer(delay, do)
            t.daemon = True
            t.start()
            self.timers.append(t)
        else:
            do()

    def detach(self, slot_id: int, delay: float = 0.0):
        """拔出一个模块（只释放槽位，业务会话不因此结束）。"""
        info = self.slots.get(slot_id)
        if not info:
            return

        def do():
            self._emit({
                'event': 'detach',
                'module_id': info['module'],
                'module_instance': info['instance'],
                'slot_id': slot_id,
                'bridge_id': 'bridge-demo',
            })

        if delay > 0:
            t = threading.Timer(delay, do)
            t.daemon = True
            t.start()
            self.timers.append(t)
        else:
            do()

    # ---- RC522 刷卡 ----
    def present_card(self, uid_hex: str, delay: float = 0.0):
        """模拟桥板级 rfid_card 事件：不带 slot/device 上下文。"""
        uid_len = len(uid_hex) // 2

        def do():
            self._emit({
                'schema': 'p0-control-v1',
                'event': 'rfid_card',
                'bridge_id': 'bridge-demo',
                'key_id': '%02x' % (int(uid_hex[:2], 16) if uid_hex else 0),
                'boot_id': '%032x' % 1,
                'session_id': '%032x' % 2,
                'uptime_ms': int(time.monotonic() * 1000),
                'event_id': self.sequence + 1,
                'uid': uid_hex,
                'uid_len': uid_len,
            })

        if delay > 0:
            t = threading.Timer(delay, do)
            t.daemon = True
            t.start()
            self.timers.append(t)
        else:
            do()

    # ---- 电池遥测 ----
    def set_battery(self, percent=None, charging=False, external=False, period=5.0):
        """周期上报电池。percent=None 表示无遥测（Pi 侧保持 unknown）。"""
        if self.battery is not None:
            self.battery['stop'] = True
        if percent is None:
            self.battery = None
            return
        state = {'percent': percent, 'charging': charging,
                 'external': external, 'stop': False}
        self.battery = state

        def loop():
            while not state['stop']:
                self._emit({
                    'schema': 'p0-control-v1',
                    'event': 'bridge_status',
                    'bridge_id': 'bridge-demo',
                    'battery_pct': state['percent'],
                    'battery_v': round(3.7 + state['percent'] * 0.009, 2),
                    'power_source': 'external' if state['external'] else 'battery',
                })
                time.sleep(period)

        t = threading.Thread(target=loop, daemon=True)
        t.start()
        self.timers.append(t)

    def close(self):
        if self.battery:
            self.battery['stop'] = True
        for t in self.timers:
            if isinstance(t, threading.Timer):
                t.cancel()
        try:
            os.close(self.master)
            os.close(self.slave)
        except OSError:
            pass
