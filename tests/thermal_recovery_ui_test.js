const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const html = fs.readFileSync(path.join(__dirname, '../esp32-birdnet-mic/webui/index.html'), 'utf8');
const ids = ['sel_oh_auto', 'therm_trip_count', 'thermal_notice', 'thermal_notice_title',
  'thermal_notice_detail', 'row_therm_latch', 'txt_therm_latch', 'therm_status',
  'btn_therm_clear', 'btn_thermal_notice_ack', 'b_srv_on', 'b_srv_off'];
const nodes = Object.fromEntries(ids.map(id => [id, {style: {}, classList: {toggle() {}}}]));
const context = vm.createContext({$: id => nodes[id] || null, Date, locks: {}, edits: {},
  toggleDirty() {}, thermalAutoPending: false});
function loadFunction(name, next) {
  const start = html.indexOf('function ' + name + '(');
  const end = html.indexOf('function ' + next + '(', start);
  assert(start >= 0 && end > start);
  vm.runInContext(html.slice(start, end), context);
}
loadFunction('renderThermalRecovery', 'loadTherm');
loadFunction('setToggleState', 'renderTimeRows');
const state = {protection_enabled: true, auto_resume_enabled: true, auto_resume_pending: true,
  latched_persist: true, notice: true, trips_24h: 1, cool_limit_c: 65, cool_remaining_sec: 300,
  can_acknowledge: false, schedule_allowed: true, last_trip_c: 81};
context.renderThermalRecovery(state);
assert.equal(nodes.thermal_notice.style.display, '');
assert.equal(nodes.btn_therm_clear.disabled, true);
assert.equal(nodes.btn_thermal_notice_ack.disabled, true);
assert.equal(nodes.therm_trip_count.textContent, '1 / 3');
assert(nodes.thermal_notice_detail.textContent.includes('65 °C'));
assert(nodes.thermal_notice_detail.textContent.includes('5:00'));
context.setToggleState(false);
assert.equal(nodes.b_srv_off.disabled, false); // User can cancel pending recovery.
context.renderThermalRecovery({...state, trips_24h: 3, manual_blocked: true,
  auto_resume_pending: false, cool_remaining_sec: 0, can_acknowledge: true});
assert(nodes.thermal_notice_title.textContent.includes('Repeated overheating'));
assert.equal(nodes.btn_therm_clear.disabled, false);
assert.equal(nodes.btn_thermal_notice_ack.textContent, 'Acknowledge and restore operation');
context.renderThermalRecovery({...state, sensor_fault: true});
assert(nodes.thermal_notice_detail.textContent.includes('sensor unavailable'));
assert.equal(nodes.btn_therm_clear.disabled, true);
context.renderThermalRecovery({...state, emergency: true, auto_resume_pending: false});
assert(nodes.thermal_notice_title.textContent.includes('Emergency'));
context.renderThermalRecovery({...state, schedule_allowed: false});
assert(nodes.thermal_notice_detail.textContent.includes('outside the allowed schedule'));
context.renderThermalRecovery({...state, latched_persist: false, auto_resume_pending: false,
  auto_resumed: true, can_acknowledge: true});
assert.equal(nodes.thermal_notice.style.display, ''); // Warning survives recovery.
assert(nodes.thermal_notice_title.textContent.includes('automatically restored'));
assert.equal(nodes.btn_therm_clear.textContent, 'Acknowledge warning');
assert.equal(nodes.btn_thermal_notice_ack.disabled, false);
context.renderThermalRecovery({...state, notice: false, latched_persist: false, auto_resume_pending: false});
assert.equal(nodes.thermal_notice.style.display, 'none');
assert.equal(nodes.row_therm_latch.style.display, 'none');
assert.equal(nodes.thermal_notice_detail.innerHTML, undefined);
console.log('Thermal UI: cooling, third-trip lockout, emergency, sensor fault, schedule, cancellation and persistent warning passed');
