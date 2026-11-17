"""Report figures. Regenerate with:
    uv run --with matplotlib python docs/report/figures.py
Writes PNGs into docs/report/fig/.
"""
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch

OUT = Path(__file__).parent / 'fig'
OUT.mkdir(exist_ok=True)

INK = '#1B2A23'
LEAF = '#E3F0E6'
WATER = '#E1EDF7'
SOIL = '#F1E9E0'
GREY = '#EEF1EF'
plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 10})


def canvas(w, h):
    fig, ax = plt.subplots(figsize=(w, h))
    ax.set_xlim(0, w)
    ax.set_ylim(0, h)
    ax.axis('off')
    return fig, ax


def box(ax, x, y, w, h, text, fill=GREY, bold=False, size=10, round_=0.12):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle=f'round,pad=0,rounding_size={round_}',
                                fc=fill, ec=INK, lw=1.2))
    ax.text(x + w / 2, y + h / 2, text, ha='center', va='center', fontsize=size,
            fontweight='bold' if bold else 'normal', color=INK, linespacing=1.35)
    return (x, y, w, h)


def arrow(ax, a, b, text='', both=False, rad=0.0, tx=0, ty=0, size=8.5, dashed=False):
    ax.annotate('', xy=b, xytext=a,
                arrowprops=dict(arrowstyle='<|-|>' if both else '-|>', color=INK, lw=1.1,
                                shrinkA=0, shrinkB=0, mutation_scale=11,
                                linestyle='--' if dashed else '-',
                                connectionstyle=f'arc3,rad={rad}'))
    if text:
        ax.text((a[0] + b[0]) / 2 + tx, (a[1] + b[1]) / 2 + ty, text, ha='center', va='center',
                fontsize=size, color=INK,
                bbox=dict(fc='white', ec='none', pad=1.2))


def save(fig, name):
    fig.savefig(OUT / name, dpi=220, bbox_inches='tight', pad_inches=0.08, facecolor='white')
    plt.close(fig)


# ---- 1. system block diagram ---------------------------------------------
fig, ax = canvas(10, 5.6)
mcu = box(ax, 3.7, 1.7, 2.6, 2.3, 'NodeMCU v1.0\nESP8266\n\nFreeRTOS firmware\nWeb server', fill=LEAF, bold=True)
s1 = box(ax, 0.2, 4.3, 2.3, 0.8, 'Soil moisture sensor', fill=SOIL)
s2 = box(ax, 0.2, 3.1, 2.3, 0.8, 'DHT11\n(temp, humidity)', fill=SOIL)
s3 = box(ax, 0.2, 1.9, 2.3, 0.8, 'Push button', fill=SOIL)
ps = box(ax, 0.2, 0.3, 2.3, 1.0, '9 V DC supply\n→ L7805 → 5 V', fill=GREY)
arrow(ax, (2.5, 4.7), (3.7, 3.7), 'A0 (analog)', tx=0.05, ty=0.18)
arrow(ax, (2.5, 3.5), (3.7, 3.2), 'D5', ty=0.12)
arrow(ax, (2.5, 2.3), (3.7, 2.4), 'D0', ty=0.12)
arrow(ax, (2.5, 0.8), (4.3, 1.7), '5 V → VIN', tx=-0.15, ty=0.05)
rl = box(ax, 7.4, 3.4, 2.3, 0.9, 'Relay module 5 V\n(active-high)', fill=WATER)
pm = box(ax, 7.4, 1.95, 2.3, 0.9, '9 V water pump\n+ 1N4007 flyback', fill=WATER)
ld = box(ax, 7.4, 0.5, 2.3, 0.9, 'Status LEDs\ngreen / red / blue', fill=GREY)
arrow(ax, (6.3, 3.6), (7.4, 3.85), 'D1', ty=0.15)
arrow(ax, (8.55, 3.4), (8.55, 2.85), 'COM → NO', tx=0.55)
arrow(ax, (6.3, 2.0), (7.4, 0.95), 'D2 / D6 / D7', tx=0.2, ty=0.12)
ph = box(ax, 3.7, 4.65, 2.6, 0.85, 'Phone / laptop browser\nhttp://plantpal.local', fill=WATER)
arrow(ax, (5.0, 4.65), (5.0, 4.0), 'Wi-Fi: hotspot or\n"PlantPal" network', both=True, tx=1.25)
save(fig, 'block-diagram.png')

# ---- 2. software architecture ---------------------------------------------
fig, ax = canvas(10, 5.4)
box(ax, 0.2, 3.3, 2.9, 1.7, 'Main loop task\n(every 50 ms)\n\nsensors · button\nwatering state machine\nLEDs', fill=LEAF)
box(ax, 3.55, 3.3, 2.9, 1.7, 'Shared state\n(mutex-guarded)\n\nsensor snapshot\npolicy · pump\nevent log (16)', fill=GREY)
box(ax, 6.9, 3.3, 2.9, 1.7, 'HTTP server task\n\npanel (gzip + ETag)\nJSON API\nlive events (SSE)', fill=WATER)
box(ax, 6.9, 0.4, 2.9, 1.9, 'Wi-Fi task\n(owns all Wi-Fi state)\n\nscan · pick · join\nfailover · AP fallback\nmDNS "plantpal"', fill=WATER)
box(ax, 3.55, 0.4, 2.9, 1.9, 'NVS flash\n\npolicy · AP switch\nsaved networks (≤5)', fill=SOIL)
box(ax, 0.2, 0.4, 2.9, 1.9, 'GPIO / ADC\n\nrelay (D1) · LEDs\nbutton · A0 · DHT11', fill=SOIL)
arrow(ax, (3.1, 4.15), (3.55, 4.15), both=True)
arrow(ax, (6.45, 4.15), (6.9, 4.15), both=True)
arrow(ax, (1.65, 3.3), (1.65, 2.3), both=True)
arrow(ax, (8.35, 3.3), (8.35, 2.3), 'message\nqueue', tx=0.55)
arrow(ax, (6.9, 1.35), (6.45, 1.35), both=True)
arrow(ax, (6.9, 3.5), (6.45, 2.2), 'settings', tx=-0.05, ty=0.12, dashed=True)
save(fig, 'software-architecture.png')

# ---- 3. watering state machine ---------------------------------------------
fig, ax = canvas(10, 4.6)
idle = box(ax, 0.3, 1.6, 2.4, 1.3, 'IDLE\npump off\nwatching moisture', fill=GREY, bold=False)
run = box(ax, 3.8, 1.6, 2.4, 1.3, 'WATERING\npump on (D1 high)\nblue LED', fill=WATER)
wait = box(ax, 7.3, 1.6, 2.4, 1.3, 'WAIT\npump off\nsoil settles', fill=LEAF)
arrow(ax, (2.7, 2.55), (3.8, 2.55), 'moisture < threshold\n(auto on)', ty=0.85)
arrow(ax, (2.7, 1.95), (3.8, 1.95), 'button or\npanel request', ty=-0.75)
arrow(ax, (6.2, 2.25), (7.3, 2.25), 'run_s elapsed\n(≤ 10 s, hard cap)', ty=1.15)
ax.annotate('', xy=(1.5, 1.6), xytext=(8.5, 1.6),
            arrowprops=dict(arrowstyle='-|>', color=INK, lw=1.1, mutation_scale=11,
                            connectionstyle='arc3,rad=-0.28'))
ax.text(5.0, 0.35, 'wait_s elapsed (auto-watering allowed again; a manual request can water during WAIT)',
        ha='center', fontsize=8.5, color=INK)
ax.text(5.0, 4.2, 'Requests that arrive while the pump is on are ignored, so runs can never chain.',
        ha='center', fontsize=8.5, color=INK, style='italic')
save(fig, 'watering-states.png')

# ---- 4. Wi-Fi manager -------------------------------------------------------
fig, ax = canvas(10, 5.2)
sc = box(ax, 0.3, 3.9, 2.2, 1.0, 'SCAN\nall channels', fill=GREY)
pk = box(ax, 3.9, 3.9, 2.2, 1.0, 'PICK\nstrongest saved\nnetwork in range', fill=GREY)
jn = box(ax, 7.5, 3.9, 2.2, 1.0, 'JOIN\n8 s timeout', fill=WATER)
cn = box(ax, 7.5, 1.3, 2.2, 1.0, 'CONNECTED\nhas IP,\nplantpal.local', fill=LEAF, bold=False)
wt = box(ax, 3.9, 1.3, 2.2, 1.0, 'WAIT\n5 s (30 s if a phone\nis on PlantPal)', fill=GREY)
arrow(ax, (2.5, 4.4), (3.9, 4.4), 'results', ty=0.2)
arrow(ax, (6.1, 4.4), (7.5, 4.4), 'found', ty=0.2)
arrow(ax, (8.6, 3.9), (8.6, 2.3), 'got IP', tx=0.45)
arrow(ax, (5.0, 3.9), (5.0, 2.3), 'none', tx=0.35)
arrow(ax, (7.5, 4.05), (6.1, 4.05), 'failed / timeout\n→ next network', ty=-0.42, size=8)
arrow(ax, (3.9, 1.8), (1.4, 3.9), 'rescan', tx=-0.35, ty=-0.1)
ax.annotate('', xy=(9.7, 4.2), xytext=(9.7, 1.9),
            arrowprops=dict(arrowstyle='-|>', color=INK, lw=1.1, mutation_scale=11,
                            connectionstyle='arc3,rad=0.6'))
ax.text(10.4, 3.05, 'lost: retry\nsame network\nonce', ha='left', va='center', fontsize=8, color=INK)
box(ax, 0.3, 0.2, 3.2, 1.6, '"PlantPal" network (192.168.4.1)\nis up when:\n• the switch is on, or\n• no networks are saved, or\n• no IP for 30 s', fill=SOIL, size=8.5)
save(fig, 'wifi-states.png')

print('figures written to', OUT)
