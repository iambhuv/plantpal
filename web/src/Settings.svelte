<script>
  import { get, post, status, elapsed } from './lib/api.js';

  let { s } = $props();

  // Per-section inline messages.
  let msg = $state({ wifi: '', water: '', dev: '' });

  // ---- Wi-Fi list ----
  const LABEL = { in_range: 'In range', away: 'Not in range', bad_pass: 'Wrong password' };
  let nets = $state([]);
  async function load() {
    try {
      nets = await get('/api/wifi');
    } catch (e) {
      msg.wifi = e.message;
    }
  }
  // Refetch on mount and whenever the station network changes.
  let netKey = $derived(`${s.net.sta}${s.net.ssid}`);
  $effect(() => {
    netKey;
    load();
  });

  let sheet, editing = $state(null), ssid = $state(''), pass = $state(''), sheetErr = $state(''), saving = $state(false);
  function openSheet(n) {
    editing = n ? n.ssid : null;
    ssid = editing || '';
    pass = '';
    sheetErr = '';
    sheet.showModal();
  }
  async function saveNet(e) {
    e.preventDefault();
    sheetErr = '';
    saving = true;
    const body = { ssid };
    if (!editing || pass) body.pass = pass; // edit with empty password = keep it
    try {
      nets = await post('/api/wifi', body);
      msg.wifi = '';
      sheet.close();
    } catch (er) {
      sheetErr = er.message;
    }
    saving = false;
  }

  // ---- Confirm dialog (Remove, Restart, AP off) ----
  let dlg, ask = $state({});
  function confirm(a) {
    ask = a;
    dlg.showModal();
  }
  function confirmed() {
    dlg.close();
    ask.run();
  }
  const remove = (n) =>
    confirm({
      title: `Remove ${n.ssid}?`,
      body: 'PlantPal will forget this network and its password.',
      label: 'Remove',
      run: async () => {
        msg.wifi = '';
        try {
          nets = await post('/api/wifi/delete', { ssid: n.ssid });
        } catch (e) {
          msg.wifi = e.message;
        }
      },
    });
  // Clear "Restarting…" once the device reports a fresh uptime.
  let restartAt = -1; // estimated uptime when Restart was sent; -1 = none
  $effect(() => {
    if (s.uptime_s < restartAt) {
      restartAt = -1;
      msg.dev = '';
    }
  });
  const restart = () =>
    confirm({
      title: 'Restart PlantPal?',
      body: 'Watering stops for a few seconds. This page reconnects on its own.',
      label: 'Restart',
      run: async () => {
        msg.dev = '';
        try {
          await post('/api/restart');
          restartAt = s.uptime_s + $elapsed;
          msg.dev = 'Restarting…';
        } catch (e) {
          msg.dev = e.message;
        }
      },
    });

  // Turning the AP off from a phone on it would cut this page off.
  const toggleAp = () =>
    s.ap_enabled && location.hostname === '192.168.4.1'
      ? confirm({
          title: 'Turn off “PlantPal” network?',
          body: 'You are connected through it, so this page may disconnect. It comes back on its own if PlantPal loses your Wi-Fi for 30 seconds.',
          label: 'Turn off',
          run: () => save({ ap_enabled: false }, 'dev'),
        })
      : save({ ap_enabled: !s.ap_enabled }, 'dev');

  // ---- Settings ----
  // Local values shown while the slider is dragged or a stepper change is pending.
  let local = $state({});
  const val = (k) => local[k] ?? s[k];
  let timer, seq = 0;

  async function save(patch, where) {
    msg[where] = '';
    const id = ++seq;
    try {
      const r = await post('/api/settings', patch);
      if (id === seq) status.set(r); // ignore a reply overtaken by a newer request
    } catch (e) {
      msg[where] = e.message;
    }
    for (const k in patch) if (local[k] === patch[k]) delete local[k];
  }
  // Slider and steppers: show at once, save once input settles, so a drag,
  // arrow-key burst or tap burst is one POST (one NVS write, one log event).
  function nudge(k, v) {
    local[k] = v;
    clearTimeout(timer);
    timer = setTimeout(() => save({ ...local }, 'water'), 600);
  }

  const WAITS = [10, 15, 20, 30, 45, 60, 90, 120, 180, 300, 600, 900, 1200, 1800];
  const fmtWait = (v) => (v < 60 ? `${v} s` : v % 60 ? `${(v / 60) | 0} min ${v % 60} s` : `${v / 60} min`);
  const fmtUp = (x) => {
    const d = (x / 86400) | 0, h = ((x / 3600) | 0) % 24, m = ((x / 60) | 0) % 60;
    return d ? `${d}d ${h}h` : h ? `${h}h ${m}m` : `${m}m`;
  };
  let wait = $derived(val('wait_s'));
  let run = $derived(val('run_s'));
</script>

<h2>Wi-Fi networks</h2>
<ul class="nets">
  {#each nets as n (n.ssid)}
    <li class="ctl">
      <div class="name">
        {n.ssid}
        <span class:bad={n.state === 'bad_pass'}>{n.state === 'connected' ? `Connected · ${s.net.ip}` : LABEL[n.state]}</span>
      </div>
      <div class="acts">
        <button class="link" onclick={() => openSheet(n)} aria-label="Edit {n.ssid}">Edit</button>
        <button class="link" onclick={() => remove(n)} aria-label="Remove {n.ssid}">Remove</button>
      </div>
    </li>
  {:else}
    <li class="ctl empty">No saved networks. PlantPal stays on its own network until you add one.</li>
  {/each}
</ul>
<button class="add" onclick={() => openSheet(null)}>Add network</button>
{#if msg.wifi}<p class="err" role="alert">{msg.wifi}</p>{/if}

<h2>Watering</h2>
<div class="ctl flat">
  <label for="thr">Water below</label><b>{val('threshold')}%</b>
</div>
<input
  id="thr" type="range" min="0" max="100" step="1"
  value={val('threshold')}
  style:--p="{val('threshold')}%"
  oninput={(e) => (local.threshold = +e.currentTarget.value)}
  onchange={(e) => nudge('threshold', +e.currentTarget.value)}
/>
<div class="ctl" role="group" aria-labelledby="run-l">
  <span id="run-l">Pump runs for</span>
  <span class="step">
    <button class="link" aria-label="Shorter pump run" disabled={run <= 1} onclick={() => nudge('run_s', run - 1)}>−</button>
    <b aria-live="polite">{run} s</b>
    <button class="link" aria-label="Longer pump run" disabled={run >= 10} onclick={() => nudge('run_s', run + 1)}>+</button>
  </span>
</div>
<div class="ctl" role="group" aria-labelledby="wait-l">
  <span id="wait-l">Wait between waterings</span>
  <span class="step">
    <button class="link" aria-label="Shorter wait" disabled={wait <= 10} onclick={() => nudge('wait_s', WAITS.filter((w) => w < wait).pop())}>−</button>
    <b aria-live="polite">{fmtWait(wait)}</b>
    <button class="link" aria-label="Longer wait" disabled={wait >= 1800} onclick={() => nudge('wait_s', WAITS.find((w) => w > wait))}>+</button>
  </span>
</div>
{#if msg.water}<p class="err" role="alert">{msg.water}</p>{/if}

<h2>Device</h2>
<button class="ctl" role="switch" aria-checked={s.ap_enabled} onclick={toggleAp}>
  <span>Fallback network “PlantPal”</span><i class="sw"></i>
</button>
<div class="ctl"><span>Uptime · free memory</span><b>{fmtUp(s.uptime_s + $elapsed)} · {Math.round(s.heap / 1024)} KB</b></div>
<div class="ctl">
  <span>Restart PlantPal</span>
  <button class="link restart" onclick={restart}>Restart</button>
</div>
{#if msg.dev}<p class={msg.dev === 'Restarting…' ? 'note' : 'err'} role="alert">{msg.dev}</p>{/if}

<dialog bind:this={sheet} aria-labelledby="sheet-t" onclose={() => (pass = '')} onclick={(e) => e.target === sheet && sheet.close()}>
  <form class="dlg" onsubmit={saveNet}>
    <h3 id="sheet-t">{editing ? 'Edit network' : 'Add network'}</h3>
    <label class="field">
      <span>Network name</span>
      <input bind:value={ssid} required maxlength="32" readonly={!!editing} autocapitalize="off" autocomplete="off" spellcheck="false" />
    </label>
    <label class="field">
      <span>Password</span>
      <input
        type="password" bind:value={pass} maxlength="64" autocomplete="new-password"
        placeholder={editing ? 'Leave empty to keep the current one' : 'Empty for an open network'}
      />
    </label>
    {#if sheetErr}<p class="err" role="alert">{sheetErr}</p>{/if}
    <div class="actions">
      <button type="button" class="cancel" onclick={() => sheet.close()}>Cancel</button>
      <button class="go" disabled={saving}>Save network</button>
    </div>
  </form>
</dialog>

<dialog bind:this={dlg} aria-labelledby="dlg-t" onclick={(e) => e.target === dlg && dlg.close()}>
  <div class="dlg">
  <h3 id="dlg-t">{ask.title}</h3>
  <p>{ask.body}</p>
  <div class="actions">
    <button class="cancel" onclick={() => dlg.close()}>Cancel</button>
    <button class="danger" onclick={confirmed}>{ask.label}</button>
  </div>
  </div>
</dialog>

<style>
  .nets { list-style: none; margin: 0; padding: 0; }
  .name { min-width: 0; overflow-wrap: anywhere; }
  .name span { display: block; font-size: 12px; color: var(--ink2); }
  .name .bad { color: var(--err); }
  .acts { display: flex; flex: none; gap: 14px; }
  .empty { color: var(--ink2); }
  .add {
    width: 100%; margin-top: 6px; padding: 11px; border-radius: 12px;
    border: 1.5px dashed var(--ink2); background: none; font-size: 14px; font-weight: 700;
  }

  .flat { border-top: 0; padding-bottom: 0; min-height: 0; }
  input[type='range'] {
    -webkit-appearance: none; appearance: none; display: block; width: 100%; height: 28px; margin: 0 0 6px; background: none;
  }
  input[type='range']:focus-visible { outline-offset: 0; }
  input[type='range']::-webkit-slider-runnable-track {
    height: 6px; border-radius: 3px; background: linear-gradient(90deg, var(--soil) var(--p), var(--line) var(--p));
  }
  input[type='range']::-moz-range-track {
    height: 6px; border-radius: 3px; background: linear-gradient(90deg, var(--soil) var(--p), var(--line) var(--p));
  }
  input[type='range']::-webkit-slider-thumb {
    -webkit-appearance: none; width: 22px; height: 22px; margin-top: -8px;
    border-radius: 50%; background: #fff; border: 2px solid var(--ink);
  }
  input[type='range']::-moz-range-thumb {
    width: 18px; height: 18px; border-radius: 50%; background: #fff; border: 2px solid var(--ink);
  }

  .step { display: flex; align-items: center; gap: 4px; }
  .step b { min-width: 76px; text-align: center; }
  .step .link { width: 36px; height: 36px; margin: -4px 0; padding: 0; font-size: 20px; line-height: 1; }
  .restart { color: var(--soil); }

  .field { display: block; margin-top: 14px; font-size: 13px; color: var(--ink2); }
  .field input {
    display: block; width: 100%; margin-top: 4px; padding: 11px 12px;
    font-size: 16px; color: var(--ink); background: var(--card);
    border: 1px solid var(--line); border-radius: 10px;
  }
  .field input[readonly] { background: none; }
</style>
