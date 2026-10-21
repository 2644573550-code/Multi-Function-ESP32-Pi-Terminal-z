#!/usr/bin/env bash
# Read-only diagnostics. Does not change the default sink, volume or routing.
set -u
echo 'ALSA playback devices:'
aplay -l 2>&1 || true
echo 'Pulse/PipeWire sinks:'
pactl list short sinks 2>&1 || true
echo 'Default sink:'
pactl get-default-sink 2>&1 || true
echo 'Mute and volume:'
pactl get-sink-mute @DEFAULT_SINK@ 2>&1 || true
pactl get-sink-volume @DEFAULT_SINK@ 2>&1 || true
echo 'Chinese TTS engine:'
if command -v espeak-ng >/dev/null; then espeak-ng --voices=cmn; else echo 'espeak-ng not installed'; fi
echo 'If no USB sound card is listed, a display USB cable may be power/touch only.'
