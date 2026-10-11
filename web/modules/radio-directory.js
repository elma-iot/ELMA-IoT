import {bundledRadioRows} from './radio-fallback.js';
const seeds = ['https://de1.api.radio-browser.info', 'https://all.api.radio-browser.info'];
const cacheKey = 'elma-radio-directory-v1';
const maxCacheAge = 30 * 86400000;

function safeServer(value) {
  try {
    const url = new URL(value.includes('://') ? value : `https://${value}`);
    return url.protocol === 'https:' && !url.username && !url.password &&
      /^[a-z0-9-]+\.api\.radio-browser\.info$/.test(url.hostname) &&
      (!url.port || url.port === '443') ? url.origin : null;
  } catch { return null; }
}

export function createRadioDirectory({ fetcher = fetch, storage = window.localStorage } = {}) {
  let servers = [...seeds], discovery = null;
  function readCache() { try { return JSON.parse(storage.getItem(cacheKey) || '{}'); } catch { return {}; } }
  function offlineRows(path) {
    const cached = readCache()[path];
    const bundled = bundledRadioRows(path);
    if (cached && Date.now()-cached.saved < maxCacheAge && Array.isArray(cached.rows) && cached.rows.length) {
      // Newly shipped countries/stations remain available even with an older cache.
      const rows = [...cached.rows];
      for (const row of bundled) {
        const exists = rows.some(old => path === '/countries'
          ? String(old.name).toLowerCase() === row.name.toLowerCase()
          : (old.url_resolved || old.url) === row.url);
        if (!exists) rows.push(row);
      }
      return { rows, cached: true, bundled: false };
    }
    return { rows: bundled, cached: false, bundled: true };
  }
  async function request(url) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 4500);
    try {
      const response = await fetcher(url, { signal: controller.signal, headers: { Accept: 'application/json' }, cache: 'no-store', credentials: 'omit' });
      if (!response.ok) throw new Error(`Directory returned HTTP ${response.status}`);
      const rows = await response.json();
      if (!Array.isArray(rows) || !rows.length) throw new Error('Directory returned no entries');
      return rows;
    } finally { clearTimeout(timer); }
  }
  async function discover() {
    for (const server of seeds) {
      try {
        const rows = await request(`${server}/json/servers`);
        const found = rows.map(row => safeServer(String(row.name || ''))).filter(Boolean);
        if (found.length) { servers = [...new Set([...found.sort(() => Math.random() - .5), ...servers])]; return; }
      } catch {}
    }
  }
  return {
    fallback(path) { return offlineRows(path); },
    async load(path) {
      let lastError;
      const attempted = new Set();
      // Try working servers first; discover mirrors only after those fail.
      for (let pass = 0; pass < 2; pass++) {
        for (const server of servers) {
          if (attempted.has(server)) continue;
          attempted.add(server);
          try {
            const rows = await request(`${server}/json${path}`);
            servers = [server, ...servers.filter(item => item !== server)];
            const fields = path === '/countries' ? ['name','stationcount','iso_3166_1','iso3166_1'] : ['name','url_resolved','url','codec','bitrate'];
            const compact = rows.map(row => Object.fromEntries(fields.filter(key => row[key] !== undefined).map(key => [key,row[key]])));
            const cache = readCache(); cache[path] = { saved: Date.now(), rows: compact };
            // Keep a bounded set of countries, not an ever-growing station archive.
            const keys = Object.keys(cache).filter(key => key !== '/countries').sort((a,b) => cache[b].saved-cache[a].saved);
            if (cache['/countries']) keys.unshift('/countries');
            try { storage.setItem(cacheKey, JSON.stringify(Object.fromEntries(keys.slice(0, 9).map(key => [key, cache[key]])))); } catch {}
            return { rows, cached: false };
          } catch (error) { lastError = error; }
        }
        if (pass === 0) { discovery ||= discover(); await discovery; }
      }
      const fallback = offlineRows(path);
      if (fallback.rows.length) return fallback;
      throw new Error(`Radio directory unavailable. Retry later; saved station URLs can still play. ${lastError?.message || ''}`);
    }
  };
}
