const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const html = fs.readFileSync(path.join(__dirname, '../esp32-birdnet-mic/webui/index.html'), 'utf8');
for (const match of html.matchAll(/<script[^>]*>([\s\S]*?)<\/script>/g)) {
  new vm.Script(match[1]);
}
const nodes = Object.fromEntries(['health_capture', 'health_stalls', 'health_failures', 'health_clients', 'health_buffer', 'health_stop', 'health_audio_load', 'health_dma', 'level', 'mqtt_discovery_status', 'mqtt_err', 'mqtt_conn', 'sel_time_zone_mode', 'row_manual_time_offset', 'row_custom_time_zone', 'sel_offset', 'btn_offset_set', 'row_offset_hint'].map(id => [id, {style: {}}]));
const context = vm.createContext({$: id => nodes[id] || null, Date, edits: {}, locks: {}, confirmedValues: {}, dirtyEls: {}, savingKeys: new Set(), pendingSyncKeys: new Set(), toggleDirty() {},
  T: {en: {clip_bad: 'Clipping', clip_warn: 'Near clipping', clip_ok: 'OK'}}, lang: 'en'});
// Compile exactly one function; neighboring dialog initialization needs a real DOM.
function loadFunction(name) {
  const start = html.indexOf('function ' + name + '(');
  assert(start >= 0);
  for (let end = html.indexOf('}', start); end >= 0; end = html.indexOf('}', end + 1)) {
    let script;
    try { script = new vm.Script(html.slice(start, end + 1)); }
    catch (error) { if (error instanceof SyntaxError) continue; throw error; }
    script.runInContext(context);
    return;
  }
  assert.fail('Incomplete function: ' + name);
}
loadFunction('renderAudioLevel');
loadFunction('updateAudioDiagnosticsUI');
loadFunction('updateMicrophoneSignal');
loadFunction('updateStreamHealth');
loadFunction('loadMqtt');
for(const name of ['fieldValue','differsFromSaved','reconcileEdit','clearDirty','syncInput'])loadFunction(name);
loadFunction('updateTimeZoneVisibility');
nodes.sel_time_zone_mode.value = '1';
context.updateTimeZoneVisibility();
assert.equal(nodes.row_manual_time_offset.style.display, 'none');
assert.equal(nodes.row_custom_time_zone.style.display, 'none');
nodes.sel_time_zone_mode.value = '0';
context.updateTimeZoneVisibility();
assert.equal(nodes.row_manual_time_offset.style.display, '');
assert.equal(nodes.sel_offset.disabled, false);
nodes.sel_time_zone_mode.value = '9';
context.updateTimeZoneVisibility();
assert.equal(nodes.row_custom_time_zone.style.display, '');
assert.equal(nodes.sel_offset.disabled, true);
context.updateStreamHealth({producer_running: true, i2s_error_count: 13, rtsp_write_stalls: 200, rtsp_write_failures: 3, rtsp_write_timeouts: 2,
  rtsp_client_teardowns: 4, rtsp_client_disconnects: 1, rb_drops: 10, rb_flushes: 8,
  last_stop_stream: 2, last_stop_reason: '<untrusted>', last_stop_time: '2026-09-08 12:00:00', last_stop_age: '1m ago'});
assert.equal(nodes.health_capture.textContent, 'Running / 13');
assert.equal(nodes.health_failures.textContent, '3 / 2');
assert.equal(nodes.health_clients.textContent, '4 / 1');
assert(nodes.health_stop.textContent.includes('Stream 2 — <untrusted>'));
assert.equal(nodes.health_stop.innerHTML, undefined);
context.updateStreamHealth({});
assert.equal(nodes.health_stop.textContent, 'No stop recorded');
assert.equal(nodes.health_audio_load.textContent, 'Unavailable');
assert.equal(nodes.health_dma.textContent, 'Unavailable');
context.updateAudioDiagnosticsUI({audio_cpu_valid: true, audio_cpu_pct: 20.1, audio_cpu_peak_pct: 45.3,
  dma_overruns_available: true, dma_overruns_1min: 0, dma_overruns_total: 5, dma_overrun_last_age: '1m ago'});
assert.equal(nodes.health_audio_load.textContent, '20.1% (peak 45.3%)');
assert.equal(nodes.health_audio_load.className, 'v');
assert.equal(nodes.health_dma.textContent, '0 / 5 (last: 1m ago)');
assert.equal(nodes.health_dma.className, 'v'); // Historical events do not keep the warning active.
context.updateAudioDiagnosticsUI({audio_cpu_valid: true, audio_cpu_pct: 65, audio_cpu_peak_pct: 90,
  dma_overruns_available: true, dma_overruns_1min: 2, dma_overruns_total: 7, dma_overrun_last_age: '<event>'});
assert.equal(nodes.health_audio_load.className, 'v warn');
assert.equal(nodes.health_dma.className, 'v warn');
assert.equal(nodes.health_dma.innerHTML, undefined); // API text stays text.
context.updateAudioDiagnosticsUI({audio_cpu_valid: true, audio_cpu_pct: 80, audio_cpu_peak_pct: 90});
assert.equal(nodes.health_audio_load.className, 'v bad');
context.updateAudioDiagnosticsUI({audio_cpu_valid: false, audio_cpu_pct: 80, audio_cpu_peak_pct: 90,
  dma_overruns_available: false, dma_overruns_1min: 0, dma_overruns_total: 0});
assert.equal(nodes.health_audio_load.textContent, 'Unavailable');
assert.equal(nodes.health_audio_load.className, 'v');
assert.equal(nodes.health_dma.textContent, 'Unavailable');
context.updateAudioDiagnosticsUI({audio_cpu_valid: true, audio_cpu_pct: NaN, audio_cpu_peak_pct: 0});
assert.equal(nodes.health_audio_load.textContent, 'Unavailable');
context.renderAudioLevel({peak_pct: 100, peak_dbfs: 0, clip: true, clip_count: 3});
assert(nodes.level.innerHTML.includes('100% (0.0 dBFS)'));
assert(!nodes.level.innerHTML.includes('-90.0'));
context.renderAudioLevel({peak_pct: 100, peak_dbfs: 0, clip: false});
assert(nodes.level.innerHTML.includes('100% (0.0 dBFS)'));
context.renderAudioLevel({peak_pct: 0, peak_dbfs: null});
assert(nodes.level.textContent.includes('0% (-90.0 dBFS)'));
context.renderAudioLevel({peak_pct: undefined, peak_dbfs: NaN});
assert(nodes.level.textContent.includes('0% (-90.0 dBFS)'));
context.loadMqtt({mqtt_connected: true, mqtt_discovery_status: 'pending / retrying', mqtt_last_error: 'discovery_publish_failed'});
assert.equal(nodes.mqtt_discovery_status.textContent, 'pending / retrying');
assert.equal(nodes.mqtt_err.textContent, 'discovery_publish_failed');
context.loadMqtt({mqtt_connected: true, mqtt_discovery_status: 'published', mqtt_last_error: 'ok'});
assert.equal(nodes.mqtt_discovery_status.textContent, 'published');
assert.equal(nodes.mqtt_err.textContent, 'ok');
console.log('Web UI health and MQTT rendering checks passed');
