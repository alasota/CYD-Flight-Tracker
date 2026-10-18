// Language switch (PL / EN) and the firmware version next to the installer.
(function () {
  var root = document.documentElement;

  function setLang(lang) {
    root.className = lang;
    root.lang = lang;
    try { localStorage.setItem('lang', lang); } catch (e) {}
    document.querySelectorAll('.lang button').forEach(function (b) {
      b.setAttribute('aria-pressed', b.dataset.lang === lang ? 'true' : 'false');
    });
  }
  document.querySelectorAll('.lang button').forEach(function (b) {
    b.addEventListener('click', function () { setLang(b.dataset.lang); });
  });
  setLang(root.className === 'pl' ? 'pl' : 'en');

  // firmware/manifest.json is added by the release workflow; without it
  // (a local preview) the installer can't work, so say so instead.
  fetch('firmware/manifest.json', { cache: 'no-cache' })
    .then(function (r) { if (!r.ok) throw new Error(r.status); return r.json(); })
    .then(function (m) { document.getElementById('fw-version').textContent = m.version; })
    .catch(function () {
      document.getElementById('installer').hidden = true;
      document.getElementById('no-firmware').hidden = false;
    });
})();
