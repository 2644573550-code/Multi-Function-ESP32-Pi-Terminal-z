#!/usr/bin/env python3
"""Validate installed/relocated ELF libraries and embedded Qt resources."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import importlib.util


def main():
    cmake, build, libdir = sys.argv[1:]
    build = Path(build).resolve()
    with tempfile.TemporaryDirectory(prefix='shared-runtime-', dir=build) as state:
        state = Path(state)
        prefix = state / 'original prefix'
        result = subprocess.run([cmake, '--install', str(build), '--prefix', str(prefix),
                                 '--component', 'TerminalRuntime'], capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr
        relocated = state / 'relocated prefix'
        prefix.rename(relocated)
        binary = relocated / 'bin/orange_pi_terminal'
        libraries = relocated / libdir
        env = os.environ.copy()
        for key in ('ORANGE_PI_CONTROL_DEVICE', 'ORANGE_PI_HOTSPOT_SUDO', 'PI_TERMINAL_RENDER_PROFILE',
                    'PI_REPAIR_SPEC', 'PI_REPAIR_IMAGE', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH'):
            env.pop(key, None)
        # Keep the host Qt SDK, but never resolve our own libraries from the build.
        env['LD_LIBRARY_PATH'] = ':'.join(p for p in env.get('LD_LIBRARY_PATH', '').split(':')
                                        if p and not Path(p).resolve().is_relative_to(build))
        env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
                   QTWEBENGINE_DISABLE_SANDBOX='1',
                   QTWEBENGINE_CHROMIUM_FLAGS='--disable-gpu --touch-events=enabled')
        for kind in ('CONFIG', 'DATA', 'STATE', 'CACHE'):
            env[f'XDG_{kind}_HOME'] = str(state / kind.lower())
        linked = subprocess.check_output(['ldd', str(binary)], env=env, text=True, timeout=10)
        assert 'not found' not in linked, linked
        for module in ('terminal_music', 'terminal_system', 'terminal_ui'):
            match = re.search(rf'lib{module}\.so\.1 => (.*?) \(', linked)
            assert match and Path(match[1]).resolve() == (libraries / f'lib{module}.so.1').resolve(), linked
        spec = importlib.util.spec_from_file_location('runtime_bundle', Path(__file__).resolve().parents[1] / 'tools/runtime_bundle.py')
        bundle = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(bundle)
        checkpoint = state / 'checkpoint/orange_pi_terminal'
        bundle.copy_runtime(build / 'orange_pi_terminal', checkpoint)
        bundle.copy_runtime(build / 'orange_pi_terminal', checkpoint)  # Idempotent, no symlink overwrite.
        different = state / 'different-binary'
        different.write_bytes(b'different runtime')
        try:
            bundle.copy_runtime(different, checkpoint)
        except RuntimeError:
            pass
        else:
            raise AssertionError('Deployment overwrote a different runtime checkpoint')
        different.write_bytes(b'libterminal_ui.so.1\0')
        try:
            bundle.copy_runtime(different, state / 'incomplete/orange_pi_terminal')
        except RuntimeError:
            pass
        else:
            raise AssertionError('Deployment accepted missing shared libraries')
        backup_linked = subprocess.check_output(['ldd', str(checkpoint)], env=env, text=True, timeout=10)
        assert 'not found' not in backup_linked, backup_linked
        for module in ('terminal_music', 'terminal_system', 'terminal_ui'):
            match = re.search(rf'lib{module}\.so\.1 => (.*?) \(', backup_linked)
            assert match and Path(match[1]).resolve().is_relative_to(checkpoint.parent), backup_linked
        result = subprocess.run([str(binary), '--window-size', '1024x600', '--quit-after', '1500'],
                                env=env, cwd=state, capture_output=True, text=True, timeout=15)
        log = result.stdout + result.stderr
        assert result.returncode == 0, log
        assert 'UI_FONT_FAMILY: Google Sans' in log, log
        assert not re.search(r'QQmlApplicationEngine failed|is not installed|No such file or directory|'
                             r'qrc:.*qml:.*(?:TypeError|ReferenceError|Cannot assign)', log), log
        print('SHARED_RUNTIME_INSTALL_PASS: relocated binary, three shared libraries, QML and fonts')


if __name__ == '__main__':
    main()
