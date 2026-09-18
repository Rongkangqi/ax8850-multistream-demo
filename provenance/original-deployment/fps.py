#!/usr/bin/env python3
"""Measured wall-clock rates from the current service invocation."""
import json
import subprocess

unit = 'ax-six-rtsp'
if subprocess.run(['systemctl', 'is-active', '--quiet', unit]).returncode:
    raise SystemExit('ax-six-rtsp is not running. Run ./start.sh first.')
inv = subprocess.check_output(['systemctl', 'show', unit, '-p', 'InvocationID', '--value'], text=True).strip()
raw = subprocess.check_output(['journalctl', '_SYSTEMD_INVOCATION_ID='+inv, '--since', '-3 minutes', '-o', 'cat', '--no-pager'], text=True)
stats = []
for line in raw.splitlines():
    try:
        event = json.loads(line)
    except ValueError:
        continue
    if event.get('event') == 'stats':
        stats.append(event)
if len(stats) < 2:
    raise SystemExit('Wait about 25 seconds after startup for two statistics samples.')
begin, end = stats[max(0, len(stats)-7)], stats[-1]
seconds = end['elapsed'] - begin['elapsed']
print(f'Measured window: {seconds:.1f} seconds (video FPS and AI FPS are different)')
print(f'{"Stream":10} {"Decode":>8} {"New video":>10} {"AI":>8} {"Encode":>8} {"Drops*":>8} {"Errors":>8}')
for a, b in zip(begin['channels'], end['channels']):
    rates = [(b[k]-a[k])/seconds for k in ['decoded', 'rendered', 'inferred', 'encoded']]
    dropped = b['render_replaced']-a['render_replaced'] + b['dropped']-a['dropped']
    errors = sum(b[k]-a[k] for k in ['mux_errors', 'submit_errors'])
    print(f'{b["name"]:10} {rates[0]:8.2f} {rates[1]:10.2f} {rates[2]:8.2f} {rates[3]:8.2f} {dropped:8d} {errors:8d}')
overview = (end['overview_encoded']-begin['overview_encoded'])/seconds
print(f'Overview encoder: {overview:.2f} FPS')
print('*Drops: render queue replacement + encoder queue drops, within this window.')
