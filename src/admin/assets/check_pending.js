// While "check now" waits for its turn (after the poll): reload until it ran
// (or expired, kUpdateManualCheckExpiryMs) — unless the user went to another tab
// or started a firmware upload meanwhile (window.upl, set by the upload script):
// leaving the page would abort the upload XHR.
setTimeout(function(){if($('tab').value=='firmware'&&!window.upl)
  location.href='/?tab=firmware'},3000);
