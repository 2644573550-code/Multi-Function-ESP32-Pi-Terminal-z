#!/usr/bin/env python3
"""虚拟双模块 + 桥板 + 电池 端到端演示（含登录）。

流程：
  1. 用 --init-admin 建管理员，拿到恢复码
  2. 起模拟桥板（真 PTY）与两个模块业务端点（真 TLS）
  3. 起终端，自动登录，插双槽模块，上报电池
  4. 依次截图：登录页 → 首页双模块 → 设置页

用法：
    python3 demo_dual_module.py <build-dir> <output-dir> --fence-so X --bp-so Y
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]          # pi_terminal/
REPO = ROOT.parent
DEVKIT = REPO / 'module-devkit' / 'examples'
sys.path.insert(0, str(ROOT / 'tools'))

import package_tool
from mock_module import services
from mock_bridge_multi import MultiSlotBridge

PIN = '246810'


def make_cert(tmp: Path, tag: str):
    cert = tmp / f'{tag}-cert.pem'
    key = tmp / f'{tag}-key.pem'
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'ec',
                    '-pkeyopt', 'ec_paramgen_curve:prime256v1', '-nodes',
                    '-keyout', str(key), '-out', str(cert), '-days', '2',
                    '-subj', f'/CN={tag}'], check=True, capture_output=True)
    der = subprocess.run(['openssl', 'x509', '-in', str(cert), '-outform', 'DER'],
                         check=True, capture_output=True).stdout
    return cert, key, hashlib.sha256(der).hexdigest()


def pack_module(tmp: Path, name: str, so: Path, qml_dir: Path, manifest_path: Path,
                arch: str) -> Path:
    plugin = tmp / ('plugin-' + name)
    plugin.mkdir(parents=True)
    shutil.copy(so, plugin / 'libmodule.so')
    manifest = json.loads(manifest_path.read_text())
    # 演示在 x86_64 主机上跑，包必须声明 x86_64 才能通过宿主校验。
    manifest['host_arch'] = sorted(set(manifest.get('host_arch', []) + [arch]))
    (plugin / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2))
    if qml_dir.exists():
        shutil.copytree(qml_dir, plugin / 'qml')
    return plugin


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('build')
    ap.add_argument('out')
    ap.add_argument('--fence-so', required=True)
    ap.add_argument('--bp-so', required=True)
    ap.add_argument('--hold', type=int, default=14, help='终端运行秒数')
    args = ap.parse_args()
    build = Path(args.build).resolve()
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)

    # 用独立 HOME，避免污染真实配置；同时让登录态可复现。
    home = out / 'home'
    if home.exists():
        shutil.rmtree(home)
    home.mkdir(parents=True)
    env = dict(os.environ,
               HOME=str(home),
               XDG_CONFIG_HOME=str(home / '.config'),
               XDG_DATA_HOME=str(home / '.local' / 'share'),
               XDG_CACHE_HOME=str(home / '.cache'))

    binary = str(build / 'orange_pi_terminal')

    # ---- 1. 建管理员（--init-admin，恢复码只打印一次）----
    init = subprocess.run([binary, '--init-admin', '演示管理员', '--init-pin-stdin'],
                          input=PIN + '\n', env=env, capture_output=True, text=True)
    (out / 'init-admin.log').write_text((init.stdout or '') + (init.stderr or ''))
    print('init-admin exit =', init.returncode)
    for line in (init.stdout or '').splitlines():
        if 'Recovery code' in line or line.strip().count('-') == 5:
            print('  ', line.strip())

    with tempfile.TemporaryDirectory(prefix='dual-demo-') as tmpdir:
        tmp = Path(tmpdir)
        bridge = MultiSlotBridge()

        cert_a, key_a, pin_a = make_cert(tmp, '82')
        cert_b, key_b, pin_b = make_cert(tmp, '83')
        tcp_a, http_a = services(tmp / 'http-a', 'demo-token', module_id=82, tls=(cert_a, key_a))
        tcp_b, http_b = services(tmp / 'http-b', 'demo-token', module_id=83, tls=(cert_b, key_b))

        subprocess.run([sys.executable, str(ROOT / 'tools' / 'package_tool.py'),
                        'keys', str(tmp / 'keys')], check=True)

        # 宿主用 QSysInfo::currentCpuArchitecture() 校验，演示要与本机一致。
        host_arch = subprocess.run(['uname', '-m'], capture_output=True, text=True
                                   ).stdout.strip() or 'x86_64'

        # 模块 .so 内嵌的 manifest 必须与包里逐字节一致（ModuleManager 会比对）。
        # 因此先用最终版 manifest 重新构建 .so，再用同一份打包。
        def build_module(example: str, out_so: Path) -> tuple[Path, Path]:
            manifest = json.loads((DEVKIT / example / 'manifest.json').read_text())
            manifest['host_arch'] = sorted(set(manifest.get('host_arch', []) + [host_arch]))
            src = tmp / ('src-' + example)
            shutil.copytree(DEVKIT / example, src)
            (src / 'manifest.json').write_text(
                json.dumps(manifest, ensure_ascii=False, indent=2))
            bld = tmp / ('build-' + example)
            subprocess.run(['cmake', '-S', str(src), '-B', str(bld),
                            '-DMODULE_SDK_DIR=' + str(REPO / 'module-devkit' / 'sdk'),
                            '-DCMAKE_PREFIX_PATH=' + os.environ.get('QT_ROOT', ''),
                            '-DCMAKE_BUILD_TYPE=Release'],
                           check=True, capture_output=True)
            subprocess.run(['cmake', '--build', str(bld), '-j4'],
                           check=True, capture_output=True)
            return bld / 'libmodule.so', src / 'manifest.json'

        fence_so, fence_manifest = build_module('xiaotian-fence', Path(args.fence_so))
        bp_so, bp_manifest = build_module('blood-pressure', Path(args.bp_so))

        fence = pack_module(tmp, 'fence', fence_so,
                            DEVKIT / 'xiaotian-fence' / 'qml', fence_manifest, host_arch)
        bp = pack_module(tmp, 'bp', bp_so,
                         DEVKIT / 'blood-pressure' / 'qml', bp_manifest, host_arch)
        package_tool.build(fence, tmp / 'http-a', str(tmp / 'keys' / 'private.pem'), host_arch)
        package_tool.build(bp, tmp / 'http-b', str(tmp / 'keys' / 'private.pem'), host_arch)

        config = {
            'python': sys.executable,
            'package_tool': str(ROOT / 'tools' / 'package_tool.py'),
            'public_key': str(tmp / 'keys' / 'public.pem'),
            'cache': str(tmp / 'cache'),
            'modules': {
                '82': {'address': '127.0.0.1', 'port': tcp_a.server_address[1],
                       'token': 'demo-token',
                       'package_url': f'http://127.0.0.1:{http_a.server_port}',
                       'transport_mode': 'fixed_hotspot_lan',
                       'package_transport': 'data', 'pin_sha256': pin_a},
                '83': {'address': '127.0.0.1', 'port': tcp_b.server_address[1],
                       'token': 'demo-token',
                       'package_url': f'http://127.0.0.1:{http_b.server_port}',
                       'transport_mode': 'fixed_hotspot_lan',
                       'package_transport': 'data', 'pin_sha256': pin_b},
            },
        }
        (tmp / 'config.json').write_text(json.dumps(config))

        run_env = dict(env,
                       ORANGE_PI_CONTROL_DEVICE=bridge.path,
                       ORANGE_PI_AUTO_TRUST='1',
                       ORANGE_PI_MODULE_INPROCESS='1',
                       ORANGE_PI_AUTO_LOGIN=PIN)

        proc = subprocess.Popen(
            ['xvfb-run', '-a', '-s', '-screen 0 1280x800x24', binary,
             '--config', str(tmp / 'config.json'),
             # --demo 注入虚拟热点租约（xvfb 环境没有 NetworkManager）
             '--demo',
             # 自动打开啸天模块页，验证自定义 QML 真的渲染出来
             '--open-module', 'fence-slot0',
             # 模块 ready 后自动触发 start，让模拟模块开始推传感器数据
             '--invoke-action', 'arm',
             '--quit-after', str(args.hold * 1000),
             '--screenshot', str(out / 'shot.png')],
            env=run_env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

        # 先让界面起来，再插模块（避开启动遮罩）。
        bridge.attach(0, module=82, instance='fence-slot0', delay=2.5)
        bridge.attach(1, module=83, instance='bp-slot1', delay=3.2)
        # 电池：82%，外部供电，每 3 秒一次。
        bridge.set_battery(percent=82, charging=False, external=True, period=3.0)
        # 4.5 秒后刷卡：无归属卡 → NO_MATCH（不弹 PIN 页）。
        bridge.present_card('a1b2c3d4', delay=6.0)

        try:
            stdout, stderr = proc.communicate(timeout=args.hold + 40)
        except subprocess.TimeoutExpired:
            proc.kill()
            stdout, stderr = proc.communicate()

        bridge.close()
        (out / 'stdout.log').write_text(stdout or '')
        (out / 'stderr.log').write_text(stderr or '')

        print(f'exit={proc.returncode}')
        for line in (stdout or '').splitlines():
            if any(k in line for k in ('Module session state', 'Session handshake',
                                       'identity', 'IDENTITY', 'battery', 'rfid')):
                print(' ', line)
        return proc.returncode


if __name__ == '__main__':
    sys.exit(main())
