#!/usr/bin/env python3
"""Diagnostic test of a real PBR1 bridge. Stop its terminal owner first.

This tests the control UART only, not plugin/data-plane acceptance.
Requires pyserial. Never run concurrently with a terminal using this device.
"""
import argparse
import binascii
import json
import struct
import time

import serial


def frame(sequence, payload, direction=0, kind=2):
    header = struct.pack('<BBIH', direction, kind, sequence, len(payload))
    return b'PBR1' + header + payload + struct.pack('<H', binascii.crc_hqx(header + payload, 0xffff))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', required=True)
    args = parser.parse_args()
    port = serial.Serial(port=None, baudrate=115200, timeout=0.05, exclusive=True)
    port.dtr = False
    port.rts = False
    port.port = args.device
    port.open()
    buffer = bytearray()
    seen = []
    raw_noise = 0

    def collect(seconds):
        nonlocal raw_noise
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            buffer.extend(port.read(4096))
            while len(buffer) >= 4:
                if buffer[:4] != b'PBR1':
                    raw_noise += 1
                    del buffer[0]
                    continue
                if len(buffer) < 14:
                    break
                direction, kind, sequence, length = struct.unpack('<BBIH', buffer[4:12])
                assert length <= 2048, 'invalid bridge length'
                if len(buffer) < 14 + length:
                    break
                wire = bytes(buffer[:14 + length])
                del buffer[:14 + length]
                assert direction == 1 and sequence > 0
                assert binascii.crc_hqx(wire[4:-2], 0xffff) == struct.unpack('<H', wire[-2:])[0], 'bridge CRC failure'
                payload = wire[12:-2].decode('utf-8')
                event = json.loads(payload) if kind == 1 else payload
                seen.append((kind, sequence, event))

    def request(sequence, command='ping'):
        return frame(sequence, json.dumps({'command': command}).encode())

    def replies(sequence):
        return [event for kind, _, event in seen if kind == 1 and event.get('request_sequence') == sequence]

    try:
        collect(3.5)
        assert any(k == 1 and e.get('event') == 'attach' and e.get('module_id') == 80 for k, _, e in seen), 'no real LinkIt attach'
        port.write(request(100)); collect(0.4)
        assert len(replies(100)) == 1
        port.write(request(100) + request(99)); collect(0.4)
        assert len(replies(100)) == 1 and not replies(99), 'replay accepted'
        bad = bytearray(request(101)); bad[-1] ^= 0x40
        port.write(bad + frame(102, b'{"command":"ping"}', direction=1)); collect(0.4)
        assert not replies(101) and not replies(102)
        port.write(b'noise' + b'PBR1' + struct.pack('<BBIH', 0, 2, 103, 65535))
        port.write(request(104)); collect(0.4)
        assert len(replies(104)) == 1, 'resync failed'
        port.write(request(105)[:15]); collect(1.3)
        port.write(request(106)); collect(0.4)
        assert not replies(105) and len(replies(106)) == 1, 'partial timeout failed'
        wire = request(107, 'status')
        for part in (wire[:3], wire[3:11], wire[11:]):
            port.write(part); time.sleep(0.03)
        collect(0.4)
        assert len(replies(107)) == 1 and replies(107)[0]['attached'] is True
        port.write(request(108) + request(109)); collect(0.4)
        assert len(replies(108)) == len(replies(109)) == 1, 'coalesced frames failed'
        port.write(frame(110, b'{"command":"ping"}trailing')); collect(0.3)
        assert not replies(110)
        port.write(request(111, 'unsupported')); collect(0.3)
        assert any(k == 3 and e == 'unsupported control command' for k, _, e in seen)
        port.write(frame(112, b'{"ignored":' + b'[' * 50 + b'0' + b']' * 50 + b',"command":"ping"}'))
        port.write(frame(113, b'{"command":"ping","ignored":"' + b'x' * 300 + b'"}'))
        port.write(request(114)); collect(0.4)
        assert not replies(112) and not replies(113) and len(replies(114)) == 1, 'parser bounds/recovery failed'
        assert raw_noise == 0, f'{raw_noise} non-PBR1 bytes on bridge UART'
        print(json.dumps({'result': 'PASS', 'frames': seen, 'non_pbr1_bytes': raw_noise}, ensure_ascii=False))
    finally:
        port.close()


if __name__ == '__main__':
    main()
