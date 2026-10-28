#!/usr/bin/env python3
"""Pi hardware-GL regression for native UI, startup, media, input and blur.

Stops the live terminal for the entire run, uses isolated XDG state, restores
the service in finally, saves full screenshots, keyboard images and JSON evidence.
No serial/profile/hotspot access, Xvfb or software-rendered acceptance.
Usage: python3 tests/screenshot_regression.py BINARY OUTPUT
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import tempfile
import struct
import zlib
import wave


def png_chunk(kind, payload):
    return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload)&0xffffffff)


def write_checkerboard(path, colors=((245,245,245),(15,25,45))):
    rows=[]
    for y in range(600):
        row=bytearray([0])
        for x in range(1024):row.extend(colors[(x//16+y//16)%2])
        rows.append(row)
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+png_chunk(b'IHDR',struct.pack('>IIBBBBB',1024,600,8,2,0,0,0))+png_chunk(b'IDAT',zlib.compress(b''.join(rows),6))+png_chunk(b'IEND',b''))


def read_png(path):
    data=path.read_bytes();assert data[:8]==b'\x89PNG\r\n\x1a\n';pos=8;payload=[];width=height=color=None
    while pos<len(data):
        size=struct.unpack('>I',data[pos:pos+4])[0];kind=data[pos+4:pos+8];part=data[pos+8:pos+8+size];pos+=12+size
        if kind==b'IHDR':width,height,depth,color,_,_,_=struct.unpack('>IIBBBBB',part)
        elif kind==b'IDAT':payload.append(part)
        elif kind==b'IEND':break
    assert depth==8 and color in (0,2,6),(path,depth,color)
    channels={0:1,2:3,6:4}[color];stride=width*channels;raw=zlib.decompress(b''.join(payload));rows=[];previous=bytearray(stride);offset=0
    for _ in range(height):
        filt=raw[offset];offset+=1;row=bytearray(raw[offset:offset+stride]);offset+=stride
        for i in range(stride):
            left=row[i-channels] if i>=channels else 0;up=previous[i];upperleft=previous[i-channels] if i>=channels else 0
            if filt==1:row[i]=(row[i]+left)&255
            elif filt==2:row[i]=(row[i]+up)&255
            elif filt==3:row[i]=(row[i]+(left+up)//2)&255
            elif filt==4:
                p=left+up-upperleft;pa=abs(p-left);pb=abs(p-up);pc=abs(p-upperleft);predict=left if pa<=pb and pa<=pc else up if pb<=pc else upperleft;row[i]=(row[i]+predict)&255
            elif filt!=0:raise AssertionError(('Unsupported PNG filter',filt))
        rows.append(row);previous=row
    return width,height,channels,rows


def image_edge_score(path,crop=None):
    width,height,channels,rows=read_png(path);x0,y0,x1,y1=crop or (1,1,width-1,height-1);total=0;count=0
    for y in range(max(1,y0),min(height-1,y1)):
        row=rows[y]
        for x in range(max(1,x0),min(width-1,x1)):
            i=x*channels;j=(x+1)*channels
            total+=sum(abs(row[i+k]-row[j+k]) for k in range(min(3,channels)))
            total+=sum(abs(row[i+k]-rows[y+1][i+k]) for k in range(min(3,channels)));count+=6
    return width,height,total/max(1,count)


def image_difference_score(first,second,crop=None):
    width,height,channels,left=read_png(first);w2,h2,c2,right=read_png(second)
    assert (width,height,channels)==(w2,h2,c2)
    x0,y0,x1,y1=crop or (0,0,width,height);total=0;count=0
    for y in range(max(0,y0),min(height,y1)):
        a=left[y];b=right[y]
        for x in range(max(0,x0),min(width,x1)):
            i=x*channels
            total+=sum(abs(a[i+k]-b[i+k]) for k in range(min(3,channels)));count+=min(3,channels)
    return total/max(1,count)


def run(binary, output, host_test=False, selected=None, quit_after_ms=6500, settle_ms=1400):
    if quit_after_ms < 6500:
        raise ValueError('Scenario runtime must be at least 6500 ms')
    if not 1400 <= settle_ms <= 60000 or quit_after_ms < settle_ms + 3000:
        raise ValueError('Settle time must be 1400..60000 ms with at least 3000 ms before exit')
    if platform.machine() not in ('aarch64', 'arm64') and not host_test:
        raise RuntimeError('Non-Pi use requires explicit --host-test; this does not validate Pi hardware GL.')
    output.mkdir(parents=True, exist_ok=True)
    service = 'pi-module-terminal.service'
    restore_service = False
    if not host_test:
        props=subprocess.check_output(['systemctl','--user','show',service,'-p','ActiveState','-p','MainPID'],text=True)
        state=dict(line.split('=',1) for line in props.splitlines() if '=' in line)
        if state.get('ActiveState')=='active':
            subprocess.run(['systemctl','--user','stop',service],check=True,timeout=40)
            assert subprocess.check_output(['systemctl','--user','show',service,'-p','MainPID','--value'],text=True).strip()=='0'
            restore_service=True
        elif state.get('ActiveState')!='inactive' or state.get('MainPID')!='0':
            raise RuntimeError(f"Refusing to test with ambiguous terminal service state: {state}")
    fixture = output / 'checkerboard.png'
    write_checkerboard(fixture)
    alternate_fixture=output/'checkerboard-alternate.png'
    write_checkerboard(alternate_fixture,((255,40,20),(20,40,255)))
    cases = []
    for language in ('zh', 'en'):
        for energy in (True, False):
            cases.append((f'startup-{language}-{energy}', dict(startup=True, energy=energy, language=language)))
        cases.append((f'startup-settings-{language}', dict(startup=True, energy=True, language=language, page=4)))
        cases.append((f'volume-{language}', dict(energy=True, language=language, volumePanel=True)))
        cases.append((f'sound-settings-{language}', dict(energy=True, language=language, page=4, soundSettings=True)))
        cases.append((f'voice-settings-{language}', dict(energy=True, language=language, page=13)))
    for enabled in (True, False):
        cases.append((f'speech-duck-{enabled}', dict(energy=True, language='en', duckPreview=enabled, quickMedia='music', mediaAction='play')))
    cases.append(('speech-duck-video', dict(energy=True, language='zh', duckPreview=True, quickMedia='video', mediaAction='play')))
    for energy in (True, False):
        for raw in (True, False):
            cases.append((f'wallpaper-{energy}-{raw}', dict(energy=energy, language='zh', wallpaper=fixture.as_uri(), backdrop=True, raw=raw)))
    cases += [
        ('wallpaper-global-ui-blur', dict(energy=False, language='zh', wallpaper=fixture.as_uri())),
        ('wallpaper-app-open-blur', dict(energy=False, language='zh', wallpaper=fixture.as_uri(), raw=True, page=1)),
        ('wallpaper-app-initial-source', dict(energy=False, language='zh', wallpaper=fixture.as_uri(), raw=True, page=1)),
        ('wallpaper-changed-after-app-open', dict(energy=False, language='zh', wallpaper=fixture.as_uri(), raw=True, page=1, wallpaperAfterOpen=alternate_fixture.as_uri())),
        ('default-wallpaper-restored-app-open', dict(energy=False, language='zh', wallpaper=fixture.as_uri(), raw=True, page=1, resetWallpaperAfterOpen=True)),
        ('wallpaper-app-no-blur-baseline', dict(energy=False, language='zh', wallpaper=fixture.as_uri(), raw=True, rawAppBlur=True, page=1, backdrop=True)),
        ('wallpaper-quick-settings-blur', dict(energy=False, language='zh', wallpaper=fixture.as_uri(), quickPanel=True)),
        ('quick-settings-no-card-blur-baseline', dict(energy=False, language='zh', wallpaper=fixture.as_uri(), raw=True, quickPanel=True, rawQuickBlur=True, backdrop=True)),
        ('quick-settings-card-blur', dict(energy=False, language='zh', wallpaper=fixture.as_uri(), raw=True, quickPanel=True, backdrop=True)),
        ('default-wallpaper-raw', dict(energy=True, language='zh', resetWallpaper=True, rawDefault=True, backdrop=True)),
        ('wallpaper-restored-default-blur', dict(energy=True, language='zh', resetWallpaper=True)),
    ]
    cases.append(('voice-test-mode-zh', dict(energy=True, language='zh', page=13, voiceTest=True)))
    cases.append(('voice-egg-confirm-zh', dict(energy=True, language='zh', page=4, eggPopup=True)))
    for language in ('zh', 'en'):
        for page in (0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 12, 13):
            cases.append((f'language-{language}-{page}', dict(energy=True, language=language, page=page)))
    cases += [('wallpaper-picker', dict(energy=False, language='en', page=4, picker=True)),
              ('paint-input-stress', dict(energy=True, language='zh', page=7, paint=True))]
    for language in ('zh', 'en'):
        for scenario in ('tracks', 'queue', 'lyrics'):
            cases.append((f'music-{language}-{scenario}', dict(energy=True, language=language, page=9, musicScenario=scenario)))
    for energy in (True,False):
        for scenario in ('tracks','queue','lyrics'):
            cases.append((f'music-colors-{energy}-{scenario}', dict(energy=energy, language='zh', page=9, musicScenario=scenario, wallpaper=fixture.as_uri(), raw=True, musicColors=True)))
    for scale in (1, 1.25, 1.5):
        cases.append((f'ime-{scale}', dict(energy=True, language='zh', page=4, keyboard=True, scale=scale, pinyin='nihao')))
    cases.append(('ime-editor-input',dict(energy=True,language='zh',page=8,keyboard=True,keyboardInput=True,pinyin='nihao')))
    for language in ('zh','en'):
        for scenario in ('sidebar','insert'):
            cases.append((f'usb-{language}-{scenario}', dict(energy=True, language=language, page=3, usbScenario=scenario)))
    for language in ('zh', 'en'):
        cases.append((f'editor-{language}-document', dict(energy=True, language=language, page=8, editorScenario=True)))
        cases.append((f'editor-{language}-file', dict(energy=True, language=language, fileEditor=True)))
    for media in ('music', 'video'):
        for action in ('play', 'pause', 'resume'):
            cases.append((f'quick-{media}-{action}', dict(energy=True, language='zh', quickMedia=media, mediaAction=action)))
    for energy in (True, False):
        cases.append((f'quick-glass-{energy}', dict(energy=energy, language='zh', page=3, wallpaper=fixture.as_uri(), raw=True, quickPanel=True, quickGlass=True, quickMedia='music', mediaAction='play')))
    if selected and 'blur-performance' in selected:
        cases.append(('blur-performance',dict(energy=True,language='zh',wallpaper=fixture.as_uri(),raw=True,blurBenchmark=True)))
    for energy in (True,False):
        cases.append((f'blur-cache-max-{energy}',dict(energy=energy,language='zh',page=3,wallpaper=fixture.as_uri(),raw=True,cacheMaximized=True)))
        cases.append((f'blur-cache-layout-{energy}',dict(energy=energy,language='zh',page=3,wallpaper=fixture.as_uri(),raw=True,quickPanel=True,quickGlass=True,quickMedia='music',mediaAction='play',cacheLayout=True)))
        cases.append((f'blur-cache-transition-{energy}',dict(energy=energy,language='zh',page=3,wallpaper=fixture.as_uri(),raw=True,cacheTransitions=True)))
        cases.append((f'blur-cache-default-{energy}',dict(energy=energy,language='zh',resetWallpaper=True,quickPanel=True,cacheDefault=True)))
    if not selected:
        cases=[case for case in cases if not case[0].startswith('blur-cache-')]
    for opacity in (0.15, 0.9):
        cases.append((f'opacity-{opacity}', dict(energy=False, language='en', page=4, opacity=opacity)))
    if selected:
        cases=[case for case in cases if case[0] in selected]
        missing=selected-{case[0] for case in cases}
        assert not missing,('Unknown scenarios',sorted(missing))
    reports = {}
    try:
        for name, spec in cases:
            with tempfile.TemporaryDirectory(prefix='state-', dir=output) as state:
                env = os.environ.copy()
                if spec.get('quickMedia'):
                    # Slow hardware scenes may wait >20 s before speech/audit.
                    # Keep the fixture playing for the full requested runtime.
                    media_seconds = max(20, (quit_after_ms + 999) // 1000 + 5)
                    home = Path(state) / 'home'
                    home.mkdir()
                    env['HOME'] = str(home)
                    with wave.open(str(home/'preview.wav'), 'wb') as wav:
                        wav.setparams((1, 2, 8000, 0, 'NONE', 'not compressed'))
                        wav.writeframes(b'\0\0' * 8000 * media_seconds)
                    if spec['quickMedia'] == 'video':
                        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i',
                                        'color=c=blue:s=160x90:r=10', '-i', str(home/'preview.wav'),
                                        '-t', str(media_seconds), '-c:v', 'mpeg4', '-c:a', 'aac', str(home/'preview.mp4')], check=True)
                for key in ('ORANGE_PI_CONTROL_DEVICE', 'ORANGE_PI_HOTSPOT_SUDO', 'PI_TERMINAL_RENDER_PROFILE', 'QT_QUICK_BACKEND'):
                    env.pop(key, None)
                for kind in ('CONFIG', 'DATA', 'STATE', 'CACHE'):
                    env[f'XDG_{kind}_HOME'] = str(Path(state) / kind.lower())
                if host_test and (spec.get('volumePanel') or 'duckPreview' in spec):
                    # Deterministic host fixtures; never write the developer's
                    # system volume or require a working speaker/TTS daemon.
                    commands = Path(state) / 'commands'
                    commands.mkdir()
                    for command, script in {
                        'pactl': '#!/bin/sh\ncase "$1" in\nget-sink-volume) echo "Volume: front-left: 41943 / 64% / -11.63 dB" ;;\nget-sink-mute) echo "Mute: no" ;;\n*) exit 2 ;;\nesac\n',
                        'espeak-ng': '#!/bin/sh\n/bin/cat >/dev/null\nexec /bin/sleep 4\n',
                    }.items():
                        path = commands / command
                        path.write_text(script)
                        path.chmod(0o700)
                    env['PATH'] = str(commands) + os.pathsep + env.get('PATH', '')
                env.update(QTWEBENGINE_DISABLE_SANDBOX='1',
                           QTWEBENGINE_CHROMIUM_FLAGS='--disable-gpu --touch-events=enabled',
                            PI_REPAIR_SPEC=json.dumps(spec), PI_REPAIR_IMAGE=str(output/f'{name}.png'),
                            PI_REPAIR_SETTLE_MS=str(settle_ms))
                if spec.get('cacheDefault'):env['PI_REPAIR_PROBE_CACHE']='1'
                if host_test:
                    env.update(QT_QPA_PLATFORM='offscreen',QT_QUICK_BACKEND='software',PI_REPAIR_HOST_TEST='1')
                    env.pop('QSG_RHI_BACKEND',None)
                else:
                    env.update(DISPLAY=':0',XAUTHORITY=str(Path.home()/'.Xauthority'),QT_QPA_PLATFORM='xcb',QSG_RHI_BACKEND='opengl')
                try:
                    result = subprocess.run([str(binary), '--window-size', '1024x600', '--quit-after', str(quit_after_ms)],
                                            # --quit-after is armed only after synchronous
                                            # Qt/QML initialization (observed >50 s on Pi).
                                            # Budget cold startup separately from runtime.
                                            env=env, capture_output=True, timeout=quit_after_ms/1000+120)
                except subprocess.TimeoutExpired as error:
                    # Preserve the last completed phase before propagating failure.
                    (output/f'{name}.log').write_text(((error.stdout or b'') + (error.stderr or b'')).decode(errors='replace'))
                    raise
                log = (result.stdout+result.stderr).decode(errors='replace')
                (output/f'{name}.log').write_text(log)
                assert result.returncode == 0, (name, result.returncode, log[-3000:])
                assert not re.search(r'(?:qrc:.*qml:.*(?:TypeError|ReferenceError|Cannot assign)|Failed to create|shader.*failed)', log), (name, log[-3000:])
                renderer = re.search(r'GL_RENDERER: ([^;\n]+)', log)
                if not host_test:
                    assert renderer and not re.search('llvmpipe|softpipe|software', renderer[1], re.I), (name, 'Hardware GL missing', log[-3000:])
                if spec.get('blurBenchmark'):
                    match=re.search(r'BLUR_BENCHMARK (\[[^\n]+\])',log)
                    assert match,(name,log[-3000:])
                    # A completely stalled measurement window is a valid (bad)
                    # baseline, not permission to drop that sample.
                    results=json.loads(match[1]);assert len(results)==20 and all(row['windowMs']>0 for row in results),results
                    reports[name]=results
                    (output/'report.json').write_text(json.dumps(reports,ensure_ascii=False,indent=2))
                    print('BLUR_BENCHMARK_PASS',name,flush=True)
                    continue
                match = re.search(r'REPAIR_AUDIT (\{[^\n]+\})', log)
                assert match, (name, log[-3000:])
                audit = json.loads(match[1])
                assert audit['startup']['complete'] and not audit['startup']['overlayLoaded'], (name, 'Startup overlay still blocks the desktop; increase settle/runtime for slow rendering', audit['startup'])
                if spec.get('volumePanel'):
                    volume = audit['volumeControl']
                    assert volume['expanded'] and volume['available'] and volume['sliderEnabled'], (name, volume)
                    assert abs(volume['volumeY'] - volume['systemY']) < 1, (name, 'Volume is not beside power controls', volume)
                    assert volume['volumeX'] >= volume['systemX'] + volume['systemWidth'], (name, volume)
                    assert volume['sliderHeight'] >= 48 and volume['sliderWidth'] >= 100, (name, volume)
                    if host_test: assert volume['volume'] == 64 and not volume['muted'], (name, volume)
                if spec.get('soundSettings'):
                    expected = 'Lower media volume during announcements' if spec['language'] == 'en' else '播报时降低媒体音量'
                    assert expected in audit['texts'], (name, audit['texts'])
                    output_label = 'Announcement output' if spec['language'] == 'en' else '播报输出设备'
                    assert output_label in audit['texts'], (name, audit['texts'])
                if name.startswith('voice-settings') or name.startswith('voice-test'):
                    microsoft = '微软声音' if spec['language'] == 'zh' else 'Microsoft voices'
                    bangdream = 'BanG Dream 测试音色'
                    assert microsoft in audit['texts'], (name, 'missing Microsoft voice section')
                    if spec.get('voiceTest'):
                        assert bangdream in audit['texts'], (name, 'test mode enabled but BanG Dream section missing')
                    else:
                        assert bangdream not in audit['texts'], (name, 'BanG Dream leaked without the easter egg')
                if 'duckPreview' in spec:
                    focus = audit['audioFocus']
                    expected = 0.2 if spec['duckPreview'] else 1.0
                    assert focus['active'] and focus['enabled'] == spec['duckPreview'], (name, focus)
                    assert abs(focus['gain'] - expected) < .001, (name, focus)
                    for kind in ('music', 'video'):
                        assert abs(audit['audioOutputs'][kind+'AudioOutput'] - focus[kind+'Volume']*expected) < .005, (name, audit['audioOutputs'], focus)
                    recovery_match = re.search(r'AUDIO_RECOVERY (\{[^\n]+\})', log)
                    assert recovery_match, (name, 'Missing post-speech recovery; allow at least 14s runtime on host')
                    recovery = json.loads(recovery_match[1])
                    assert not recovery['active'] and recovery['gain'] == 1, (name, recovery)
                    for kind in ('music', 'video'):
                        assert not recovery[kind+'Playing'], (name, 'Manually paused media was restarted', recovery)
                        assert abs(recovery[kind+'Output'] - .45) < .005, (name, 'User volume changed during speech was overwritten', recovery)
                    assert recovery['videoMuted'] and not recovery['musicMuted'], (name, 'Mute state changed on recovery', recovery)
                    audit['audioRecovery'] = recovery
                if spec.get('startup'):
                    early_match = re.search(r'STARTUP_AUDIT (\{[^\n]+\})', log)
                    assert early_match, (name, 'Missing first-frame startup audit')
                    early = json.loads(early_match[1])
                    assert early['overlayLoaded'] and not early['complete'] and early['focused'], (name, early)
                    assert early['language'] == spec['language'] and early['energy'] == spec['energy'], (name, early)
                    assert 0 <= early['progress'] <= 1 and early['status'], (name, early)
                    content = early['content']
                    assert content and content['x'] >= 0 and content['y'] >= 0, (name, early)
                    assert content['x'] + content['width'] <= early['width'] + 1 and content['y'] + content['height'] <= early['height'] + 1, (name, early)
                    assert image_edge_score(output/f'{name}.png.startup.png')[2] > .02, (name, 'Blank startup screenshot')
                    ready_match = re.search(r'STARTUP_READY (\{[^\n]+\})', log)
                    assert ready_match, (name, 'Missing real startup completion')
                    ready = json.loads(ready_match[1])
                    if not host_test:
                        assert ready['cacheReady'] and not ready['usingFallback'], (name, 'Pi unexpectedly needed startup fallback', ready)
                    audit['startupFirstFrame'] = early
                    audit['startupCompletion'] = ready
                if spec.get('cacheMaximized') or spec.get('cacheLayout') or spec.get('cacheTransitions') or spec.get('cacheDefault'):
                    cache=audit['blurCache']
                    if not host_test:
                        assert cache['windowReady'] and cache['panelReady'],(name,cache)
                        assert cache['windowRevision']==cache['windowGeneration'] and cache['panelRevision']==cache['panelGeneration'],(name,'Unpublished/stale texture',cache)
                    for key in ('x','y','width','height'):
                        if not spec.get('cacheDefault'):
                            assert abs(audit['appBackdropRect'][key]-audit['appBackdropExpected'][key])<1,(name,'Stale app sampling coordinates',key,audit['appBackdropRect'],audit['appBackdropExpected'])
                    if spec.get('cacheDefault') and not host_test:
                        assert cache['windowAlpha']>=250 and cache['panelAlpha']>=250,(name,'Native cached FBO is not opaque',cache)
                        w,h,c,rows=read_png(output/f'{name}.png.cache.png')
                        assert c==4,(name,'Cache alpha inspection missing')
                        alpha=[rows[y][x*4+3] for y in range(16,h-16,32) for x in range(16,w-16,32)]
                        assert alpha and min(alpha)>=250,(name,'Default wallpaper cache contains transparent/empty regions',min(alpha) if alpha else None)
                    if spec.get('cacheMaximized'):assert audit['maximized'],(name,audit)
                    if spec.get('cacheTransitions'):
                        for action in ('maximize','panel-open','panel-change'):
                            frames=sorted(output.glob(f'{name}.png.{action}*.png'))
                            assert len(frames)==4,(name,action,len(frames))
                            for frame in frames:
                                # These frames are retained for visual review;
                                # dimension validation needs only IHDR, not 12
                                # extra full-image Python unfilter passes on Pi.
                                with frame.open('rb') as stream:header=stream.read(33)
                                assert header[:8]==b'\x89PNG\r\n\x1a\n' and header[12:16]==b'IHDR',(name,frame)
                                w,h,depth,color=struct.unpack('>IIBB',header[16:26])
                                assert w>=320 and h>=320 and depth==8 and color in (0,2,6),(name,frame,w,h)
                if spec.get('musicColors'):
                    palette=audit['musicPalette']
                    assert palette['dark'] and palette['background']=='#121212', (name,palette)
                    assert palette['textColors'] and set(palette['textColors'])<={'#ffffff','#b3b3b3','#1ed760','#ffdaa0'}, (name,palette)
                    assert '#b3b3b3' in palette['textColors'], (name,'Secondary text lost its hierarchy',palette)
                if spec.get('editorScenario'):
                    metrics = audit['editorMetrics']
                    assert metrics['topPadding'] >= 0 and metrics['cursor']['y'] >= 0, (name, metrics)
                    assert metrics['viewportHeight'] >= 100, (name, metrics)
                if spec.get('quickMedia'):
                    media = audit['mediaCard']
                    assert media['visible'] and media['background'], (name, media)
                    assert media['video'] == (spec['quickMedia'] == 'video'), (name, media)
                    assert media['playing'] == (spec['mediaAction'] != 'pause'), (name, media)
                    assert media['duration'] > 0 and media['position'] > 0, (name, media)
                    assert not (media['musicPlaying'] and media['videoPlaying']), (name, media)
                if spec.get('quickGlass'):
                    surfaces = audit['quickGlass']
                    panel=next(s for s in surfaces if s['name']=='quickPanelBlur')
                    bounds=audit['blurSourceSize']
                    assert panel['sample']['x']>=-1 and panel['sample']['y']>=-1,(name,'Panel starts outside blur source',panel,bounds)
                    assert panel['sample']['x']+panel['sample']['width']<=bounds['width']+1 and panel['sample']['y']+panel['sample']['height']<=bounds['height']+1,(name,'Panel blur must cover the footer region too',panel,bounds)
                    assert 0 <= audit['firstBlurCapturePosition'] < 1, (name, 'Blur capture must start before the panel finishes opening', audit['firstBlurCapturePosition'])
                    assert {s['name'] for s in surfaces} == {'quickPanelBlur','quickHotspotBlur','quickBluetoothBlur','quickSystemBlur','quickVolumeBlur','quickMediaBlur','quickNotificationBlur'}, (name, surfaces)
                    for surface in surfaces:
                        assert surface['enabled'], (name, surface)
                        assert not surface['live'], (name, 'Quick panel must use static snapshots', surface)
                        if not host_test:
                            assert surface['cached'], (name, 'Quick panel did not reuse the shared blur texture', surface)
                        assert surface['levels'] == (2 if spec['energy'] else 4), (name, surface)
                        assert surface['resolution'] == (.25 if spec['energy'] else .5), (name, surface)
                        for key in ('x','y','width','height'):
                            assert abs(surface['sample'][key]-surface[key]) < 1, (name, surface)
                    size = audit['quickTileSize']
                    assert abs(size['width']-size['statusWidth']) < 1 and abs(size['height']-size['statusHeight']) < 1, (name, size)
                assert audit['energy'] == spec['energy'] and audit['language'] == spec['language'], (name, audit)
                assert audit['backdropLevels'] == (2 if spec['energy'] else 4), (name, audit)
                assert audit['powerIconCases']=={'battery':'battery_3_bar','charging':'battery_charging_full','external':'power','other':'battery_unknown'}, (name,audit['powerIconCases'])
                assert audit['homeBlur'] == (not spec.get('rawDefault')), (name, 'Home background blur state mismatch', audit)
                if not spec.get('backdrop'):
                    assert audit['statusDockBlur'], (name, 'Global status dock blur missing', audit)
                if spec.get('wallpaper') and not spec.get('raw'):
                    assert audit['wallpaperReady'] and audit['wallpaperBlur'], (name, 'Selected wallpaper must be blurred', audit)
                    if not spec.get('backdrop') and not spec.get('page'):
                        assert audit['launcherCardBlur'], (name, 'Home app-card backdrop blur missing', audit)
                if spec.get('quickPanel'):
                    assert audit['quickDate'] and '[object Object]' not in audit['quickDate'], (name, 'Invalid localized date', audit['quickDate'])
                    if spec['language']=='zh':
                        assert re.search(r'\d{4}年\d{1,2}月\d{1,2}日', audit['quickDate']), (name, audit['quickDate'])
                    assert audit['quickCardBlur'] == (not spec.get('rawQuickBlur')), (name, 'Quick-settings tile background blur state mismatch', audit)
                    assert audit['quickBackdropAttached'], (name, 'Quick-settings card has no wallpaper sampler', audit)
                if spec.get('page')==1 and spec.get('wallpaper'):
                    assert audit['appBlur']==(not spec.get('rawAppBlur')) and audit['appBackdropLive'], (name, 'Opened app backdrop blur missing/stale sampler', audit)
                    if spec.get('wallpaperAfterOpen'):
                        assert audit['wallpaperUrl']==spec['wallpaperAfterOpen'] and audit['appBackdropRefreshCount']>0, (name,'wallpaper update did not refresh the live app background',audit)
                    if spec.get('resetWallpaperAfterOpen'):
                        assert audit['wallpaperUrl']=='' and not audit['wallpaperReady'] and audit['appBlur'] and audit['appBackdropRefreshCount']>0,(name,'default wallpaper did not refresh open app backdrop',audit)
                if spec.get('wallpaper') and not spec.get('page') and not spec.get('backdrop'):
                    samples=audit['launcherCardSamples']
                    assert len(samples)>=2 and len({round(item['x']) for item in samples})>=2,samples
                    assert all(item['width']<1024*.65 and item['height']<600*.65 for item in samples),samples
                if spec.get('resetWallpaper'):
                    assert not audit['wallpaperReady'] and not audit['wallpaperBlur'], (name, 'Custom wallpaper was not reset', audit)
                    assert audit['homeBlur'] == (not spec.get('rawDefault')), (name, 'Default background blur state mismatch', audit)
                if not host_test:assert 'FBO_BLUR_RENDER' in log, (name, 'No real FBO draw')
                frame_path=output/f'{name}.png';frame_width,frame_height,channels,pixels=read_png(frame_path)
                assert frame_width == 1024 and 500 <= frame_height <= 600, (name, (frame_width,frame_height))
                assert image_edge_score(frame_path)[2] > .02, (name, 'Blank screenshot')
                if spec.get('wallpaper') and not spec.get('resetWallpaperAfterOpen'):
                    assert audit['wallpaperReady'] and audit['wallpaperBlur'] == (not spec.get('raw')), (name, audit)
                if spec['language'] == 'en':
                    # Browser page content/title is supplied by the visited site;
                    # only terminal chrome is subject to the global UI locale.
                    if spec.get('page') not in (2, 3, 8):
                        assert not audit['untranslated'], (name, audit['untranslated'])
                    assert all(not re.search(r'[\u4e00-\u9fff]',text) for text in audit['languageSamples']), (name,audit['languageSamples'])
                if spec.get('page') == 6:
                    assert not audit['maximized'], 'Calculator must open restored'
                if spec.get('page') == 4:
                    performance = 'Performance' if spec['language'] == 'en' else '性能'
                    rendering = 'Energy-saving rendering' if spec['language'] == 'en' else '节能渲染'
                    if not spec.get('soundSettings'): # sound-search intentionally hides unrelated groups
                        assert performance in audit['texts'] and rendering in audit['texts'], (name, 'Performance group wording missing')
                    assert not {'性能与输入', '节能渲染、键盘和输入方案', 'Performance & input', 'Energy saving, keyboard and input'}.intersection(audit['texts']), (name, 'Obsolete performance/input wording')
                if spec.get('page'):
                    assert abs(audit['windowOpacity']-1) < .01, (name, 'Foreground opacity changed')
                if 'opacity' in spec:
                    assert abs(audit['tintOpacity']-spec['opacity']) < .01, (name, audit)
                if spec.get('picker'):
                    assert audit['picker'], 'Wallpaper button did not open picker'
                recommendations = audit['recommendations']
                for code, word in [('ni','你'),('hao','好'),('nihao','你好'),('zhongguo','中国'),('women','我们'),('sentence','你好中国')]:
                    assert recommendations[code] and recommendations[code][0] == word, (name, code, recommendations[code][:8])
                if spec.get('keyboard'):
                    keyboard = audit['keyboard']
                    assert keyboard['internal'], (name, 'Keyboard must share the terminal window')
                    assert keyboard['width'] <= 680 and keyboard['height'] <= 268, (name, keyboard['width'], keyboard['height'])
                    assert keyboard['clampPassed'] and keyboard['x']>=0 and keyboard['y']>=0, (name, keyboard)
                    assert keyboard['x']+keyboard['width']<=keyboard['hostWidth'] and keyboard['y']+keyboard['height']<=keyboard['hostHeight'], (name, keyboard)
                    if spec.get('keyboardInput'):assert keyboard['inputPassed'], (name,'IME commit/backspace/English input failed')
                    assert keyboard['visible'] and len(keyboard['bounds']) >= 29 and len(keyboard['candidates']) >= 4, (name, keyboard)
                    for rect in keyboard['bounds']:
                        assert rect['x'] >= -.5 and rect['y'] >= -.5 and rect['x']+rect['width'] <= keyboard['width']+.5 and rect['y']+rect['height'] <= keyboard['height']+.5, (name, rect, keyboard['width'], keyboard['height'])
                    assert (output/f'{name}.png.keyboard.png').is_file(), (name, 'No keyboard screenshot')
                if spec.get('usbScenario'):
                    assert len(audit['usbButtons'])==2, (name,audit['usbButtons'])
                    button=audit['usbButtons'][0]
                    assert button['y']>=0 and button['y']+button['height']<=button['viewport'], (name,button)
                    assert audit['usbPrompt']==(spec['usbScenario']=='insert'), (name,audit['usbPrompt'])
                if spec.get('paint'):
                    assert len(audit['paint']['strokes']) == 30 and min(audit['paint']['strokes']) >= 2, (name, audit['paint'])
                    canvas=audit['paint'];x0=int(canvas['x'])+5;y0=int(canvas['y'])+5;x1=int(canvas['x']+canvas['width'])-5;y1=int(canvas['y']+canvas['height'])-5
                    dark=sum(1 for y in range(max(0,y0),min(frame_height,y1)) for x in range(max(0,x0),min(frame_width,x1)) if pixels[y][x*channels]<150 and pixels[y][x*channels+1]<150 and pixels[y][x*channels+2]<150)
                    assert dark>(x1-x0)*5, (name,'No continuous painted lines',dark)
                reports[name] = audit
                (output/'report.json').write_text(json.dumps(reports, ensure_ascii=False, indent=2))
                print('SCREENSHOT_PASS', name, flush=True)
        if host_test:
            reports['blurPixelComparison']='NOT_VALIDATED: software/offscreen rendering is not evidence of GPU FBO blur'
        else:
            for energy in (True, False):
                raw_path=output/f'wallpaper-{energy}-True.png';blur_path=output/f'wallpaper-{energy}-False.png'
                if raw_path.exists() and blur_path.exists():
                    _,_,rawEdges=image_edge_score(raw_path,(40,40,980,480))
                    _,_,blurredEdges=image_edge_score(blur_path,(40,40,980,480))
                    evidence=dict(rawEdges=rawEdges, blurredEdges=blurredEdges)
                    assert evidence['blurredEdges'] < evidence['rawEdges']*.75, ('Wallpaper blur has no visible effect',energy,evidence)
                    reports[f'blur-pixels-{energy}']=evidence
            raw_default=output/'default-wallpaper-raw.png';blur_default=output/'wallpaper-restored-default-blur.png'
            if raw_default.exists() and blur_default.exists():
                difference=image_difference_score(raw_default,blur_default,(350,470,680,520))
                evidence=dict(rawVsBlurredMeanDifference=difference,region=[350,470,680,520])
                assert difference>0.5, ('Default wallpaper blur did not restore',evidence)
                reports['blur-pixels-default-wallpaper']=evidence
            quick_raw=output/'quick-settings-no-card-blur-baseline.png';quick_blurred=output/'quick-settings-card-blur.png'
            if quick_raw.exists() and quick_blurred.exists():
                difference=image_difference_score(quick_raw,quick_blurred,(24,96,270,158))
                evidence=dict(rawVsBlurredMeanDifference=difference,region=[24,96,270,158])
                assert difference>0.5, ('Quick-settings card blur has no visible pixel effect',evidence)
                reports['blur-pixels-quick-settings-card']=evidence
            app_raw=output/'wallpaper-app-no-blur-baseline.png';app_blurred=output/'wallpaper-app-open-blur.png'
            if app_raw.exists() and app_blurred.exists():
                difference=image_difference_score(app_raw,app_blurred,(84,40,940,475))
                evidence=dict(rawVsBlurredMeanDifference=difference,region=[84,40,940,475])
                assert difference>0.5, ('Opened-app wallpaper blur has no visible pixel effect',evidence)
                reports['blur-pixels-open-app']=evidence
            app_initial=output/'wallpaper-app-initial-source.png';app_changed=output/'wallpaper-changed-after-app-open.png'
            if app_initial.exists() and app_changed.exists():
                difference=image_difference_score(app_initial,app_changed,(84,40,940,475))
                evidence=dict(initialVsUpdatedWallpaperMeanDifference=difference,region=[84,40,940,475])
                assert difference>3, ('App backdrop kept the old wallpaper after an in-place change',evidence)
                reports['wallpaper-live-app-update']=evidence
            app_default=output/'default-wallpaper-restored-app-open.png'
            if app_initial.exists() and app_default.exists():
                difference=image_difference_score(app_initial,app_default,(84,40,940,475))
                evidence=dict(customVsDefaultMeanDifference=difference,region=[84,40,940,475])
                assert difference>3, ('Open app kept custom wallpaper after reset to default',evidence)
                reports['wallpaper-live-app-default-restore']=evidence
        reports['binarySha256']=hashlib.sha256(binary.read_bytes()).hexdigest()
        (output/'report.json').write_text(json.dumps(reports, ensure_ascii=False, indent=2))
        print('HOST_SCREENSHOT_PASS' if host_test else 'SCREENSHOT_REGRESSION_PASS', len(cases), 'host-only/non-GL scenarios' if host_test else 'hardware-GL scenarios', flush=True)
    finally:
        if restore_service:subprocess.run(['systemctl', '--user', 'start', service],check=True,timeout=40)


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--host-test',action='store_true',help='Run isolated x86 desktop/offscreen checks, never Pi hardware-GL acceptance')
    parser.add_argument('--only',action='append',help='Run only these scenarios; repeat for each scenario name')
    parser.add_argument('--quit-after-ms',type=int,default=6500,help='Scenario runtime; increase for slower Pi hardware (minimum 6500)')
    parser.add_argument('--settle-ms',type=int,default=1400,help='Wait before audit capture on slower hardware (1400..60000)')
    args=parser.parse_args()
    if args.host_test:
        args.output.mkdir(parents=True,exist_ok=True)
    run(args.binary.resolve(),args.output.resolve(),args.host_test,set(args.only or []),args.quit_after_ms,args.settle_ms)
