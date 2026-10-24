(function () {
  var busy = false, allowLeave = false, disabled = [], recoveryDisabled = [];
  var otaAllowed = document.body.dataset.otaAllowed !== '0';
  var otaMessage = document.getElementById('ota-preparation-required');
  var overlay = document.getElementById('ota-progress');
  var title = document.getElementById('ota-progress-title');
  var detail = document.getElementById('ota-progress-text');
  var reconnect = document.getElementById('ota-reconnect');
  var space = document.getElementById('update-space');
  var prepare = document.getElementById('update-space-prepare');
  var pendingKey = 'updateSpacePending', waiting = false, polling = false;

  function remembered() { try { return sessionStorage.getItem(pendingKey) === '1'; } catch (_) { return waiting; } }
  function remember(on) { waiting = on; try { if (on) sessionStorage.setItem(pendingKey, '1'); else sessionStorage.removeItem(pendingKey); } catch (_) {} }
  function setText(t, d) { title.textContent = t; detail.textContent = d; }
  function begin(t, d) {
    if (!busy) {
      disabled = Array.from(document.querySelectorAll('button:not(#ota-reconnect),input,a.btn')).map(function (el) {
        return {el: el, disabled: el.disabled, aria: el.getAttribute('aria-disabled')};
      });
      disabled.forEach(function (item) { item.el.disabled = true; item.el.setAttribute('aria-disabled', 'true'); });
    }
    busy = true;
    reconnect.hidden = true;
    setText(t, d);
    overlay.classList.add('active');
  }
  function end() {
    busy = false;
    overlay.classList.remove('active');
    disabled.forEach(function (item) {
      item.el.disabled = item.disabled;
      if (item.aria === null) item.el.removeAttribute('aria-disabled');
      else item.el.setAttribute('aria-disabled', item.aria);
    });
    disabled = [];
    setOtaAllowed(otaAllowed, otaMessage ? otaMessage.textContent : '');
  }
  window.addEventListener('beforeunload', function (e) {
    if (busy && !allowLeave) { e.preventDefault(); e.returnValue = 'Device update is in progress.'; }
  });
  function spaceStatus() {
    var controller = new AbortController();
    var timeout = setTimeout(function () { controller.abort(); }, 8000);
    return fetch('/api/update_space', {cache: 'no-store', signal: controller.signal}).then(function (r) {
      if (!r.ok) throw new Error('not ready');
      return r.json();
    }).finally(function () { clearTimeout(timeout); });
  }
  function setOtaAllowed(allowed, message) {
    otaAllowed = allowed;
    if (otaMessage) {
      otaMessage.hidden = allowed;
      otaMessage.textContent = message || 'Please prepare your device first using the Prepare device button above.';
    }
    if (!busy) document.querySelectorAll('#ota-install-form button,#ota-upload-form button,#ota-upload-form input').forEach(function (el) {
      el.disabled = !allowed;
    });
  }
  function requirePreparation() {
    if (otaAllowed) return false;
    if (space && !space.hidden) space.scrollIntoView({behavior: 'smooth', block: 'center'});
    if (prepare && !prepare.hidden && !prepare.disabled) prepare.focus();
    return true;
  }
  function showSpace(s) {
    if (!space) return;
    if (s.state !== 'recovery_required') {
      recoveryDisabled.forEach(function (item) {
        item.el.disabled = item.disabled;
        if (item.aria === null) item.el.removeAttribute('aria-disabled');
        else item.el.setAttribute('aria-disabled', item.aria);
      });
      recoveryDisabled = [];
    }
    setOtaAllowed(s.ota_allowed === true, s.ota_message);
    space.hidden = s.state === 'unsupported';
    prepare.hidden = s.state === 'ready' || s.state === 'check_failed';
    prepare.disabled = false;
    document.getElementById('update-space-title').textContent = s.state === 'ready' ? 'Ready for future updates' : s.state === 'check_failed' ? 'Could not check update preparation' : 'Prepare for future updates';
    document.getElementById('update-space-text').textContent = s.state === 'ready'
      ? 'More space is available for firmware. Future updates work as usual. Your settings are kept.'
      : 'Make more space for future firmware with one preparation step. Your settings are kept.';
    document.getElementById('update-space-result').textContent = s.message || (s.state === 'check_failed' ? s.ota_message : '') || '';
    prepare.textContent = s.state === 'error' || s.state === 'recovery_required' ? 'Retry preparation' : 'Prepare device';
    if (s.state === 'recovery_required') {
      // The server also blocks other mutations while the table needs recovery.
      if (!recoveryDisabled.length) recoveryDisabled = Array.from(document.querySelectorAll('button:not(#ota-reconnect),input,a.btn')).map(function (el) {
        return {el: el, disabled: el.disabled, aria: el.getAttribute('aria-disabled')};
      });
      recoveryDisabled.forEach(function (item) { item.el.disabled = true; item.el.setAttribute('aria-disabled', 'true'); });
      prepare.disabled = false;
      prepare.removeAttribute('aria-disabled');
    }
  }
  function pollPreparation() {
    if (polling) return;
    polling = true;
    var deadline = Date.now() + 180000;
    function poll() {
      spaceStatus().then(function (s) {
        if (s.state === 'working') {
          setText('Preparing device…', 'Do not disconnect power. The device may restart twice; this page reconnects automatically.');
          return false;
        }
        polling = false;
        remember(false);
        end();
        showSpace(s);
        document.getElementById('update-space-result').textContent = s.state === 'ready'
          ? 'Preparation complete. You can use your device and install future updates as usual.'
          : s.message || 'Preparation did not finish. You can try again.';
        return true;
      }).catch(function () { return false; }).then(function (done) {
        if (done) return;
        if (Date.now() < deadline) { setTimeout(poll, 1500); return; }
        polling = false;
        setText('Waiting for your device', 'Reconnection is taking longer than expected. Keep power connected and check again.');
        reconnect.hidden = false;
      });
    }
    poll();
  }
  reconnect.addEventListener('click', function () {
    reconnect.hidden = true;
    if (remembered()) pollPreparation(); else waitForDevice();
  });
  function replaceWithResponse(r) {
    busy = false;
    allowLeave = true;
    return r.text().then(function (t) { document.open(); document.write(t); document.close(); });
  }
  function waitForDevice() {
    setText('Firmware installed', 'The device is rebooting. This page will reconnect automatically…');
    var tries = 0;
    setTimeout(function poll() {
      fetch('/api/status?ota_reconnect=' + Date.now(), {cache: 'no-store', signal: AbortSignal.timeout(8000)}).then(function (r) {
        if (!r.ok) throw new Error('not ready');
        allowLeave = true;
        location.replace('/ota');
      }).catch(function () {
        if (++tries < 120) setTimeout(poll, 1500);
        else { setText('Waiting for your device', 'Reconnection is taking longer than expected. Keep power connected and check again.'); reconnect.hidden = false; }
      });
    }, 4000);
  }
  function handleResponse(r) {
    if (r.headers.get('X-ESP32MIC-OTA-Reboot') === '1') return r.text().then(waitForDevice);
    return replaceWithResponse(r);
  }
  function fail(e) { end(); alert('Update request failed: ' + e); }
  if (prepare) {
    prepare.addEventListener('click', function () {
      if (busy || !confirm('Prepare this device for future updates? Settings are kept and the stream will stop briefly. Keep power connected. If power fails during preparation, USB recovery may be needed.')) return;
      remember(true);
      begin('Preparing device…', 'Do not disconnect power. Restarts and reconnection are automatic.');
      fetch('/api/update_space/prepare', {method: 'POST', cache: 'no-store', headers: {'X-ESP32MIC-CSRF': '1'}, signal: AbortSignal.timeout(8000)}).then(function (r) {
        return r.json().then(function (s) {
          if (!r.ok || !s.ok) {
            remember(false);
            end();
            document.getElementById('update-space-result').textContent = s.error || 'Preparation could not start. Please try again.';
            return;
          }
          pollPreparation();
        });
      }).catch(pollPreparation); // A lost acknowledgement must not start a second migration.
    });
    spaceStatus().then(function (s) {
      if (s.state === 'working' || remembered()) {
        remember(true);
        begin('Checking update preparation…', 'Waiting for the device. Keep power connected.');
        pollPreparation();
      } else showSpace(s);
    }).catch(function () {
      if (remembered()) { begin('Reconnecting to your device…', 'Keep power connected.'); pollPreparation(); }
    });
  }
  var install = document.getElementById('ota-install-form');
  if (install) install.addEventListener('submit', function (e) {
    e.preventDefault();
    if (busy || requirePreparation() || !confirm('Install firmware update now? The stream will stop and the device will reboot.')) return;
    begin('Downloading and installing firmware…', 'Do not disconnect power or close this page. The stream will stop temporarily.');
    fetch('/ota/install', {method: 'POST', cache: 'no-store', headers: {'X-ESP32MIC-CSRF': '1'}}).then(handleResponse).catch(fail);
  });
  var upload = document.getElementById('ota-upload-form');
  if (upload) upload.addEventListener('submit', function (e) {
    e.preventDefault();
    if (busy || requirePreparation()) return;
    if (!confirm('Upload and install selected firmware now?')) return;
    var uploadData = new FormData(upload);
    begin('Uploading and installing firmware…', 'Do not disconnect power or close this page. The stream will stop temporarily.');
    fetch('/ota/upload', {method: 'POST', cache: 'no-store', headers: {'X-ESP32MIC-CSRF': '1'}, body: uploadData}).then(handleResponse).catch(fail);
  });
  if (document.body.dataset.otaChecking === '1') {
    var tries = 0;
    function pollVersion() {
      if (busy) { setTimeout(pollVersion, 750); return; }
      fetch('/api/ota_status', {cache: 'no-store'}).then(function (r) { return r.json(); }).then(function (j) {
        if (busy || j && j.checking && tries++ < 10) setTimeout(pollVersion, 750);
        else location.replace('/ota');
      }).catch(function () { if (tries++ < 10) setTimeout(pollVersion, 750); });
    }
    setTimeout(pollVersion, 750);
  }
})();
