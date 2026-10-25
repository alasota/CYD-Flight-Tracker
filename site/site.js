// Language switch (PL / EN), the settings-page viewer's tabs and the firmware
// version next to the installer.
(function () {
  var root = document.documentElement;
  // CSS hides the inactive screenshots of the viewer only under html.js, so
  // without JS all of them stay visible. Hence classList everywhere below:
  // setting className would drop "js" on every language switch.
  root.classList.add('js');

  function setLang(lang) {
    root.classList.remove('en', 'pl');
    root.classList.add(lang);
    root.lang = lang;
    try { localStorage.setItem('lang', lang); } catch (e) {}
    document.querySelectorAll('.lang button').forEach(function (b) {
      b.setAttribute('aria-pressed', b.dataset.lang === lang ? 'true' : 'false');
    });
  }
  document.querySelectorAll('.lang button').forEach(function (b) {
    b.addEventListener('click', function () { setLang(b.dataset.lang); });
  });
  setLang(root.classList.contains('pl') ? 'pl' : 'en');

  // Settings-page viewer (#panel): one tabpanel at a time (class "active"), click or
  // left / right arrow (Home / End) to switch; roving tabindex as in the WAI-ARIA tabs pattern.
  document.querySelectorAll('.panel-viewer').forEach(function (viewer) {
    var tabs = Array.prototype.slice.call(viewer.querySelectorAll('[role="tab"]'));
    function select(tab, focus) {
      tabs.forEach(function (t) {
        var on = t === tab;
        t.setAttribute('aria-selected', on ? 'true' : 'false');
        t.tabIndex = on ? 0 : -1;
        document.getElementById(t.getAttribute('aria-controls')).classList.toggle('active', on);
      });
      if (focus) tab.focus();
    }
    tabs.forEach(function (t, i) {
      t.addEventListener('click', function () { select(t, false); });
      t.addEventListener('keydown', function (e) {
        var j = { ArrowRight: i + 1, ArrowLeft: i - 1, Home: 0, End: tabs.length - 1 }[e.key];
        if (j === undefined) return;
        e.preventDefault();
        select(tabs[(j + tabs.length) % tabs.length], true);
      });
    });
    var current = tabs.filter(function (t) { return t.getAttribute('aria-selected') === 'true'; })[0];
    if (current || tabs[0]) select(current || tabs[0], false);
  });

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
