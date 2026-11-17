"""PlantPal schematic. Regenerate with:
    uv run --with schemdraw --with matplotlib python docs/circuit/schematic.py
Writes plantpal-schematic.svg and .png next to this file.
"""
from pathlib import Path

import schemdraw
import schemdraw.elements as elm

schemdraw.use('matplotlib')
OUT = Path(__file__).parent

INK = '#1B2A23'
PWR = '#B03A2E'    # 3V3 / 5V nets
SIG = '#2B7BB5'    # GPIO signals
LOAD = '#7B5A3D'   # 9 V pump side
P = elm.IcPin


def vcc(d, at, name, color=PWR, stub=(0, 0)):
    """Stub from a pin to a named supply symbol (points up)."""
    end = (at[0] + stub[0], at[1] + stub[1])
    if stub != (0, 0):
        d.add(elm.Line(color=color).at(at).to(end))
    d.add(elm.Vdd(color=color).at(end).theta(0).label(name, color=color, fontsize=10))


def gnd(d, at, stub=(0, 0)):
    end = (at[0] + stub[0], at[1] + stub[1])
    if stub != (0, 0):
        d.add(elm.Line().at(at).to(end))
    d.add(elm.Ground().at(end).theta(0))


def module(d, title, pins, size, anchor, at):
    """Box for a breakout module, placed un-rotated with `anchor` pin at `at`."""
    m = d.add(elm.Ic(pins=pins, size=size, pinspacing=0.8, plblsize=10)
              .anchor(anchor).at(at).theta(0))
    pts = list(m.absanchors.values())
    top = max(p[1] for p in pts)
    cx = (min(p[0] for p in pts) + max(p[0] for p in pts)) / 2
    d.add(elm.Label().at((cx, top + 0.8)).label(title, fontsize=10, halign='center'))
    return m


with schemdraw.Drawing(show=False) as d:
    d.config(fontsize=11, unit=2.0, color=INK, lw=1.4)

    # ---- NodeMCU -----------------------------------------------------------
    mcu = d.add(elm.Ic(
        pins=[P(name='VIN', side='left', slot='14/14', anchorname='VIN'),
              P(name='3V3', side='left', slot='11/14', anchorname='V33'),
              P(name='GND', side='left', slot='8/14', anchorname='GND'),
              P(name='A0', side='left', slot='3/14', anchorname='A0'),
              P(name='D1  GPIO5', side='right', slot='14/14', anchorname='D1'),
              P(name='D5  GPIO14', side='right', slot='11/14', anchorname='D5'),
              P(name='D0  GPIO16', side='right', slot='8/14', anchorname='D0'),
              P(name='D2  GPIO4', side='right', slot='5/14', anchorname='D2'),
              P(name='D6  GPIO12', side='right', slot='3/14', anchorname='D6'),
              P(name='D7  GPIO13', side='right', slot='1/14', anchorname='D7')],
        size=(5.2, 11.6), pinspacing=0.8, plblsize=11).at((0, 0)).theta(0))
    R, L = mcu.D1[0], mcu.VIN[0]
    d.add(elm.Label().at(((R + L) / 2, mcu.VIN[1] + 1.6))
          .label('NodeMCU v1.0  (ESP8266 / ESP-12F)', fontsize=12, halign='center'))

    # left: supply pins
    vcc(d, mcu.VIN, '5V', stub=(-1.2, 0))
    vcc(d, mcu.V33, '3V3', stub=(-1.2, 0))
    gnd(d, mcu.GND, stub=(-1.2, 0))

    # ---- soil moisture sensor -> A0 ----------------------------------------
    ms = module(d, 'Soil moisture sensor',
                [P(name='VCC', side='right', slot='3/3', anchorname='VCC'),
                 P(name='AO', side='right', slot='2/3', anchorname='AO'),
                 P(name='GND', side='right', slot='1/3', anchorname='G')],
                (2.6, 2.4), 'AO', (L - 4.0, mcu.A0[1]))
    d.add(elm.Line(color=SIG).at(ms.AO).to(mcu.A0))
    vcc(d, ms.VCC, '3V3', stub=(1.0, 0))
    gnd(d, ms.G, stub=(1.0, 0))

    # ---- power: +9 V -> L7805 -> 5 V ----------------------------------------
    py = mcu.VIN[1] + 3.2
    reg = d.add(elm.VoltageRegulator().at((L - 7.0, py)).theta(0))
    d.add(elm.Label().at((reg.center[0], reg.center[1] + 1.3)).label('L7805', fontsize=10))
    vcc(d, reg.absanchors['in'], '+9V in', color=LOAD, stub=(-1.2, 0))
    vcc(d, reg.out, '5V', stub=(1.2, 0))
    gnd(d, reg.gnd)

    # ---- relay module on D1, pump on NO --------------------------------------
    rl = module(d, 'Relay module 5V\n(JQC3F-05VDC-C, active-HIGH)',
                [P(name='VCC', side='left', slot='3/3', anchorname='VCC'),
                 P(name='GND', side='left', slot='2/3', anchorname='G'),
                 P(name='IN', side='left', slot='1/3', anchorname='IN'),
                 P(name='COM', side='right', slot='3/3', anchorname='COM'),
                 P(name='NO', side='right', slot='2/3', anchorname='NO'),
                 P(name='NC', side='right', slot='1/3', anchorname='NC')],
                (3.0, 2.4), 'IN', (R + 7.0, mcu.D1[1]))
    d.add(elm.Line(color=SIG).at(mcu.D1).to(rl.IN))
    vcc(d, rl.VCC, '5V', stub=(-0.9, 0))
    gnd(d, rl.G, stub=(-0.9, 0))
    vcc(d, rl.COM, '+9V', color=LOAD, stub=(1.0, 0))
    d.add(elm.Label().at(rl.NC).label('  NC (unused)', fontsize=8, color='#888', halign='left'))

    top = (rl.NO[0] + 2.6, rl.NO[1])
    d.add(elm.Line(color=LOAD).at(rl.NO).to(top))
    d.add(elm.Dot(color=LOAD).at(top))
    pump = d.add(elm.Motor(color=LOAD).at(top).down().length(2.4).label('9V\npump', loc='top', ofst=0.2))
    bot = pump.end
    d.add(elm.Dot(color=LOAD).at(bot))
    gnd(d, bot, stub=(0, -0.4))
    d.add(elm.Line(color=LOAD).at(top).right(1.6))
    d.add(elm.Diode(color=LOAD).down().length(2.4).reverse().label('1N4007\n(band up)', loc='bottom', ofst=0.2))
    d.add(elm.Line(color=LOAD).left(1.6))

    # ---- DHT11 on D5 with 10k pull-up --------------------------------------
    x_pu = R + 2.0
    d.add(elm.Line(color=SIG).at(mcu.D5).to((x_pu, mcu.D5[1])))
    d.add(elm.Dot(color=SIG))
    pu = d.add(elm.Resistor().up().length(1.4).label('10k', loc='bottom', fontsize=9))
    vcc(d, pu.end, '3V3')
    dht = module(d, 'DHT11',
                 [P(name='DATA', side='left', slot='3/3', anchorname='DATA'),
                  P(name='VCC', side='left', slot='2/3', anchorname='VCC'),
                  P(name='GND', side='left', slot='1/3', anchorname='G')],
                 (2.2, 2.4), 'DATA', (R + 7.0, mcu.D5[1]))
    d.add(elm.Line(color=SIG).at((x_pu, mcu.D5[1])).to(dht.DATA))
    vcc(d, dht.VCC, '3V3', stub=(-0.9, 0))
    gnd(d, dht.G, stub=(-0.9, 0))

    # ---- button on D0 with 10k pull-up -------------------------------------
    d.add(elm.Line(color=SIG).at(mcu.D0).to((x_pu, mcu.D0[1])))
    d.add(elm.Dot(color=SIG))
    pu = d.add(elm.Resistor().up().length(1.4).label('10k', loc='bottom', fontsize=9))
    vcc(d, pu.end, '3V3')
    d.add(elm.Line(color=SIG).at((x_pu, mcu.D0[1])).right(2.2))
    d.add(elm.Button().right().label('Water button', loc='top', fontsize=9))
    gnd(d, d.here, stub=(0.6, 0))

    # ---- status LEDs: 3V3 -> 330R -> LED -> pin (active-low) ---------------
    for pin, name in (('D2', 'Green: soil OK'), ('D6', 'Red: soil dry'), ('D7', 'Blue: pump on')):
        a = getattr(mcu, pin)
        d.add(elm.Line(color=SIG).at(a).right(1.2))
        d.add(elm.LED().right().reverse())
        d.add(elm.Resistor().right().label('330R', loc='top', fontsize=9))
        end = d.here
        vcc(d, end, '3V3', stub=(0.5, 0))
        d.add(elm.Label().at((end[0] + 2.0, end[1])).label(name, fontsize=9, halign='left'))

    d.save(str(OUT / 'plantpal-schematic.svg'))
    d.save(str(OUT / 'plantpal-schematic.png'), dpi=200)
