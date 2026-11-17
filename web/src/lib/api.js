import { writable } from 'svelte/store';

// Latest status JSON from the device (null until the first message).
export const status = writable(null);
// True while the event stream is open.
export const connected = writable(false);

// Whole seconds since `status` was last set. Frames only arrive on real
// changes (or every ~10-15 s), so relative times add this to the device's
// uptime_s / last_watered_s. A local 1 s ticker, not polling.
export const elapsed = writable(0);
let since = Date.now();
status.subscribe(() => {
  since = Date.now();
  elapsed.set(0);
});
setInterval(() => elapsed.set(Math.floor((Date.now() - since) / 1000)), 1000);

let retry;

// The browser reconnects on its own (server sends `retry: 2000`); only when it
// gives up (CLOSED, e.g. a non-200 during restart) do we open a new stream.
function connect() {
  const es = new EventSource('/api/events');
  es.onopen = () => connected.set(true);
  es.onmessage = (e) => {
    status.set(JSON.parse(e.data));
    connected.set(true);
  };
  es.onerror = () => {
    connected.set(false);
    if (es.readyState === EventSource.CLOSED && !retry) {
      retry = setTimeout(() => {
        retry = 0;
        connect();
      }, 2000);
    }
  };
}
connect();

async function request(path, opts) {
  let res, json;
  try {
    res = await fetch(path, opts);
    json = await res.json();
  } catch (e) {
    // network error or non-JSON body: fall through to the generic message
  }
  if (!res || !res.ok || json === undefined) throw new Error((json && json.error) || 'Request failed');
  return json;
}

export const get = (path) => request(path);

export const post = (path, body) =>
  request(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body || {}),
  });
