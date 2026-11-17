<script>
  import { post, status, elapsed } from './lib/api.js';
  import SoilColumn from './SoilColumn.svelte';

  let { s } = $props();
  let err = $state('');
  let busy = $state(false);

  const ago = (x) =>
    x < 0 ? 'never' : x < 60 ? 'just now' : x < 3600 ? `${(x / 60) | 0}m ago` : x < 86400 ? `${(x / 3600) | 0}h ago` : `${(x / 86400) | 0}d ago`;

  const SRC = { water_auto: 'auto', water_button: 'button', water_panel: 'panel' };
  const describe = (e) =>
    SRC[e.k] ? `Watered ${Math.round(e.n / 1000)} s (${SRC[e.k]})`
    : e.k === 'soil_dry' ? `Soil dropped to ${e.n}%`
    : e.k === 'wifi_joined' ? `Joined ${e.s}`
    : e.k === 'wifi_lost' ? 'Lost Wi-Fi'
    : e.k === 'settings' ? 'Settings changed'
    : 'Event';

  let uptime = $derived(s.uptime_s + $elapsed);
  let state = $derived(s.pump_on ? 'Watering' : s.moisture_pct >= s.threshold ? 'Soil is moist' : 'Soil is dry');

  async function act(path, body) {
    err = '';
    busy = true;
    try {
      const r = await post(path, body);
      if (r.events) status.set(r);
    } catch (e) {
      err = e.message;
    }
    busy = false;
  }
</script>

<div class="hero">
  <SoilColumn pct={s.moisture_pct} threshold={s.threshold} watering={s.pump_on} />
  <div>
    <div class="v">{s.moisture_pct}<small>%</small></div>
    <p class="s" aria-live="polite">{state}</p>
    <p class="t">{s.auto ? `Waters below ${s.threshold}%` : 'Automatic watering is off'}</p>
  </div>
</div>

<div class="tiles">
  <div><span>Air</span><b>{s.dht_ok ? `${s.temp_c.toFixed(1)}°C` : '–'}</b></div>
  <div><span>Humidity</span><b>{s.dht_ok ? `${s.humidity}%` : '–'}</b></div>
  <div><span>Last watered</span><b>{ago(s.last_watered_s < 0 ? -1 : s.last_watered_s + $elapsed)}</b></div>
</div>

<button class="primary" disabled={busy || s.pump_on} onclick={() => act('/api/water')}>Water now</button>

<button class="ctl" role="switch" aria-checked={s.auto} disabled={busy} onclick={() => act('/api/settings', { auto: !s.auto })}>
  <span>Water automatically</span><i class="sw"></i>
</button>

{#if err}<p class="err" role="alert">{err}</p>{/if}

<h2>Recent</h2>
<ul class="log">
  {#each s.events.slice(0, 3) as e}
    <li><span>{describe(e)}</span><span>{ago(uptime - e.t)}</span></li>
  {:else}
    <li>Nothing yet</li>
  {/each}
</ul>

<style>
  .hero { display: flex; gap: 18px; align-items: flex-end; margin: 22px 0 18px; }
  .v { font-size: 64px; font-weight: 800; line-height: .9; letter-spacing: -.04em; font-variant-numeric: tabular-nums; }
  .v small { font-size: 26px; letter-spacing: 0; }
  .s { margin: 8px 0 0; font-size: 15px; }
  .t { margin: 4px 0 0; font-size: 12px; color: var(--ink2); }

  .tiles { display: flex; gap: 10px; margin-bottom: 14px; }
  .tiles div { flex: 1; min-width: 0; padding-top: 8px; border-top: 2px solid var(--line); }
  .tiles span { display: block; font-size: 12px; color: var(--ink2); }
  .tiles b { font-size: 20px; font-variant-numeric: tabular-nums; white-space: nowrap; }

  .primary { margin: 4px 0 12px; }

  .log { list-style: none; margin: 0; padding: 0; font-size: 13px; color: var(--ink2); }
  .log li { display: flex; justify-content: space-between; gap: 12px; padding: 6px 0; border-top: 1px solid var(--line); }
  .log li span:last-child { flex: none; font-variant-numeric: tabular-nums; }
</style>
