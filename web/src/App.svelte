<script>
  import { status, connected } from './lib/api.js';
  import Plant from './Plant.svelte';
  import Settings from './Settings.svelte';

  let route = $state(location.hash);
  $effect(() => {
    const onHash = () => (route = location.hash);
    addEventListener('hashchange', onHash);
    return () => removeEventListener('hashchange', onHash);
  });
  let settings = $derived(route === '#/settings');
</script>

<div class="page">
  <header>
    <h1>{settings ? 'Settings' : 'PlantPal'}</h1>
    <p class="net" aria-live="polite">
      {#if !$status}Connecting…
      {:else if !$connected}Reconnecting…
      {:else}on <b>{$status.net.sta ? $status.net.ssid : 'PlantPal network'}</b>{/if}
    </p>
  </header>

  <main class:stale={!$connected}>
    {#if $status}
      {#if settings}<Settings s={$status} />{:else}<Plant s={$status} />{/if}
    {/if}
  </main>
</div>

<nav aria-label="Sections">
  <div class="tray">
    <a href="#/" aria-current={settings ? undefined : 'page'}>Plant</a>
    <a href="#/settings" aria-current={settings ? 'page' : undefined}>Settings</a>
  </div>
</nav>

<style>
  .page {
    max-width: 420px; margin: 0 auto;
    padding: max(18px, env(safe-area-inset-top)) 20px calc(88px + env(safe-area-inset-bottom));
  }
  header { display: flex; justify-content: space-between; align-items: baseline; gap: 12px; }
  h1 { margin: 0; font-weight: 800; font-size: 22px; letter-spacing: -.02em; }
  .net { margin: 0; font-size: 12px; color: var(--ink2); text-align: right; overflow-wrap: anywhere; }
  .net b { color: var(--leaf); font-weight: 700; }
  main { transition: opacity .3s; }
  main.stale { opacity: .55; }

  nav {
    position: fixed; left: 0; right: 0; bottom: 0;
    padding: 8px 20px max(10px, env(safe-area-inset-bottom));
    background: var(--glass); border-top: 1px solid var(--line);
  }
  .tray { display: flex; gap: 6px; max-width: 380px; margin: 0 auto; padding: 4px; border-radius: 12px; background: var(--tray); }
  a {
    flex: 1; padding: 9px; border-radius: 9px;
    text-align: center; text-decoration: none;
    font-size: 13px; font-weight: 700; color: var(--ink2);
  }
  a[aria-current] { background: var(--card); color: var(--ink); }
</style>
