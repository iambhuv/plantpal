// Dev-only fake PlantPal API so the panel runs without the board:
//   DEVICE=mock bun run dev
// Loaded by vite.config.js only in that mode (apply: 'serve'), never bundled.

export default function mock() {
  return {
    name: 'plantpal-mock',
    apply: 'serve',
    configureServer(server) {
      const st = {
        moisture_raw: 0, moisture_pct: 44, temp_c: 24.5, humidity: 58, dht_ok: true,
        pump_on: false, auto: true, threshold: 40, run_s: 3, wait_s: 60,
        last_watered_s: -1, uptime_s: 8040, heap: 31744, ap_enabled: true,
        net: { sta: true, ssid: "Phone hotspot", ip: '192.168.20.4', rssi: -52, ap: false },
        events: [
          { t: 7200, k: 'water_auto', n: 3000, s: '' },
          { t: 6100, k: 'soil_dry', n: 39, s: '' },
          { t: 60, k: 'wifi_joined', n: 0, s: "Phone hotspot" },
        ],
      };
      let lastEnd = 7203, pumpEnd = 0, src = '', down = 0, sent = '', lastPush = 0;
      const nets = [
        { ssid: "Phone hotspot", state: 'connected', rssi: -52 },
        { ssid: "Friend's hotspot", state: 'away', rssi: 0 },
        { ssid: 'Hostel-2F', state: 'bad_pass', rssi: -70 },
      ];
      const clients = new Set();
      const log = (k, n = 0, s = '') => {
        st.events.unshift({ t: st.uptime_s, k, n, s });
        st.events.length = Math.min(st.events.length, 16);
      };
      const water = (k) => {
        if (st.pump_on) return;
        st.pump_on = true;
        pumpEnd = st.uptime_s + st.run_s;
        src = k;
      };
      const frame = () => `data: ${JSON.stringify(st)}\n\n`;

      setInterval(() => {
        st.uptime_s++;
        if (st.pump_on) {
          st.moisture_pct = Math.min(100, st.moisture_pct + 5);
          if (st.uptime_s >= pumpEnd) {
            st.pump_on = false;
            lastEnd = st.uptime_s;
            log(src, st.run_s * 1000);
          }
        } else if (st.uptime_s % 4 === 0 && st.moisture_pct > 0) {
          st.moisture_pct--;
          if (st.moisture_pct === st.threshold - 1) log('soil_dry', st.moisture_pct);
        }
        st.last_watered_s = lastEnd < 0 ? -1 : st.uptime_s - lastEnd;
        if (st.auto && !st.pump_on && st.moisture_pct < st.threshold && st.uptime_s - lastEnd >= st.wait_s) water('water_auto');
        st.moisture_raw = 1024 - st.moisture_pct * 6;
        st.heap = 31744 + ((st.uptime_s * 37) % 900);
        // Like the firmware: push on real changes, else a refresh every ~12 s.
        const { uptime_s, heap, last_watered_s, ...rest } = st;
        const key = JSON.stringify(rest);
        if (key !== sent || st.uptime_s - lastPush >= 12) {
          sent = key;
          lastPush = st.uptime_s;
          for (const c of clients) c.write(frame());
        }
      }, 1000);

      server.middlewares.use(async (req, res, next) => {
        const url = req.url.split('?')[0];
        if (!url.startsWith('/api/')) return next();
        const send = (code, body) => {
          res.statusCode = code;
          res.setHeader('Content-Type', 'application/json');
          res.end(JSON.stringify(body));
        };
        if (Date.now() < down) {
          res.statusCode = 503; // "rebooting": EventSource goes CLOSED, panel retries in 2 s
          return res.end();
        }

        if (req.method === 'GET') {
          if (url === '/api/status') return send(200, st);
          if (url === '/api/wifi') return send(200, nets);
          if (url === '/api/events') {
            res.writeHead(200, { 'Content-Type': 'text/event-stream', 'Cache-Control': 'no-cache' });
            res.write('retry: 2000\n\n' + frame());
            clients.add(res);
            req.on('close', () => clients.delete(res));
            return;
          }
          return next();
        }

        if (req.headers['content-type'] !== 'application/json')
          return send(415, { error: 'Content-Type must be application/json' });
        let raw = '';
        for await (const c of req) raw += c;
        if (raw.length > 512) return send(400, { error: 'body too large' });
        let b;
        try {
          b = JSON.parse(raw);
        } catch (e) {}
        if (!b || typeof b !== 'object' || Array.isArray(b)) return send(400, { error: 'invalid JSON' });
        await new Promise((r) => setTimeout(r, 250)); // feel some latency

        switch (url) {
          case '/api/water':
            water('water_panel');
            return send(200, { ok: true });

          case '/api/settings': {
            for (const k of ['auto', 'ap_enabled'])
              if (k in b && typeof b[k] !== 'boolean') return send(400, { error: 'auto and ap_enabled must be booleans' });
            for (const k of ['threshold', 'run_s', 'wait_s'])
              if (k in b && !Number.isInteger(b[k])) return send(400, { error: 'threshold, run_s and wait_s must be integers' });
            const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
            if ('auto' in b) st.auto = b.auto;
            if ('ap_enabled' in b) st.ap_enabled = b.ap_enabled;
            if ('threshold' in b) st.threshold = clamp(b.threshold, 0, 100);
            if ('run_s' in b) st.run_s = clamp(b.run_s, 1, 10);
            if ('wait_s' in b) st.wait_s = clamp(b.wait_s, 10, 1800);
            log('settings');
            return send(200, st);
          }

          case '/api/wifi': {
            const p = b.pass;
            if (typeof b.ssid !== 'string' || (p != null && typeof p !== 'string'))
              return send(400, { error: 'ssid must be a string, pass a string if given' });
            if (!b.ssid || b.ssid.length > 32 || (p && (p.length < 8 || p.length > 64)))
              return send(400, { error: 'ssid 1-32 chars, password empty or 8-64 chars' });
            const n = nets.find((x) => x.ssid === b.ssid);
            if (n) {
              if (typeof p === 'string' && n.state === 'bad_pass') n.state = 'in_range';
            } else {
              if (nets.length >= 5) return send(409, { error: '5 networks saved, remove one first' });
              nets.push({ ssid: b.ssid, state: 'in_range', rssi: -66 });
            }
            return send(200, nets);
          }

          case '/api/wifi/delete': {
            const i = nets.findIndex((x) => x.ssid === b.ssid);
            if (i < 0) return send(404, { error: 'network not saved' });
            nets.splice(i, 1);
            return send(200, nets);
          }

          case '/api/restart':
            send(200, { ok: true });
            setTimeout(() => {
              down = Date.now() + 4000;
              for (const c of clients) c.end();
              clients.clear();
              Object.assign(st, { uptime_s: 0, events: [], pump_on: false, last_watered_s: -1 });
              lastEnd = -1e9;
            }, 500);
            return;
        }
        next();
      });
    },
  };
}
