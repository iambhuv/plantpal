<script>
  let { pct, threshold, watering } = $props();
</script>

<div class="col" class:watering role="img" aria-label="Soil moisture {pct}%, waters below {threshold}%">
  <div class="wet" style:height="{pct}%"></div>
  <div class="thr" style:bottom="{threshold}%"></div>
</div>

<style>
  .col {
    flex: none; width: 92px; height: 210px; position: relative; overflow: hidden;
    border: 2px solid var(--soil); border-radius: 18px 18px 26px 26px; background: var(--earth);
  }
  .wet {
    position: absolute; left: 0; right: 0; bottom: 0;
    background: linear-gradient(var(--wet-top), var(--wet));
    transition: height .8s ease;
  }
  .watering .wet { background: linear-gradient(#5B9BC9, var(--water)); }
  .thr {
    position: absolute; left: -2px; right: -2px; height: 0;
    border-top: 2px dashed var(--ink);
    transition: bottom .4s ease;
  }
</style>
