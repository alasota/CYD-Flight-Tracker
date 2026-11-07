// The settings page's script (config_page.cpp writes it after `var T={...};`, the texts from
// web_i18n in the page's language). tools/embed_assets.py joins the lines with nothing in
// between: no comment after code, and no line may end and the next start with a letter, digit,
// `_`, `$` or quote (that would glue two tokens).
function $(i){return document.getElementById(i)}

// Tab switching; the language-toggle forms follow the current tab.
function show(t){
  document.querySelectorAll('section[data-tab]').forEach(function(s){s.hidden=s.dataset.tab!=t});
  document.querySelectorAll('.tabs button').forEach(function(b){b.classList.toggle('on',b.dataset.t==t)});
  $('tab').value=t;
  $('save').hidden=t=='backup'||t=='firmware';
  document.querySelectorAll('.lang input[name=tab]').forEach(function(i){i.value=t})}
document.querySelectorAll('.tabs button').forEach(function(b){b.onclick=function(){show(b.dataset.t)}});

// The live credit readouts, mirroring openSkyCreditCost() (scan_area), openSkyDailyCredits()
// (config_page) and openSkyDailyLimit() (opensky_auth). In LoginFailed (#daily carries
// data-lf) the limit stays 400 until the credentials are edited.
function cost(r){var a=4*r*r;return a<=25?1:a<=100?2:a<=400?3:4}
function calc(){
  var c=cost(parseFloat($('radius').value)||0),p=Math.max(parseInt($('poll').value)||0,1),
    n=parseInt($('daily').dataset.night)||0,
    d=Math.ceil((86400-($('ntp').checked&&$('ntm').value!='off'?n*60:0))/p)*c,
    lf=$('daily').dataset.lf&&$('cid').value==$('cid').defaultValue&&!$('cs').value,
    l=!lf&&$('cid').value&&($('cs').value||$('cs').dataset.saved)?4000:400;
  $('cost').textContent=c;
  $('ntsv').textContent=Math.ceil(n*60/p);
  $('daily').innerHTML='≈ <b>'+d+'</b> '+T.day+' · '+T.lim+' '+l+
    (d>l?'<div class=warn>'+T.over+'</div>':'')}
['radius','poll','cid','cs'].forEach(function(i){$(i).addEventListener('input',calc)});

// Screen card: show only the fields of the chosen modes; the "Now" line
// follows the board (GET /light.json) while the General tab is open.
function disp(){
  var a=$('blm').value=='auto',n=$('ntm').value;
  $('blauto').hidden=!a;
  $('blfix').hidden=a;
  $('ntsch').hidden=n!='schedule';
  $('ntopt').hidden=n=='off'}
$('blm').onchange=disp;
$('ntm').onchange=function(){disp();calc()};
$('ntp').onchange=calc;
disp();
setInterval(function(){
  if($('tab').value!='general')return;
  fetch('/light.json',{cache:'no-store'}).then(function(r){return r.json()}).then(function(j){
    $('lr').textContent=j.raw;
    $('la').textContent=j.avg;
    $('lp').textContent=j.pct;
    $('ls').textContent=(!j.synced?T.lns:j.screen_off?T.loff:j.woken?T.lwk:j.night?T.lnt:T.lday)+
      (j.polling?'':' · '+T.lpz)}).catch(function(){})},2000);
function pw(){var i=$('cs');i.type=i.type=='password'?'text':'password'}

// Firmware upload with a progress bar (a plain form post works too, just
// without one). The answer page replaces this one.
$('fw').onsubmit=function(e){
  var i=$('fwf').files[0];
  if(!i)return;
  e.preventDefault();
  if(/-full/i.test(i.name)){$('fws').textContent=T.full;return}
  var x=new XMLHttpRequest(),d=new FormData();
  d.append('firmware',i);
  x.open('POST','/update?size='+i.size);
  x.upload.onprogress=function(v){
    if(!v.lengthComputable)return;
    var p=Math.round(v.loaded*100/v.total);
    $('fwp').hidden=false;
    $('fwp').value=p;
    $('fws').textContent=T.up+' '+p+'%'};
  x.onload=function(){document.open();document.write(x.responseText);document.close()};
  x.onerror=function(){window.upl=0;$('fwb').disabled=false;$('fws').textContent=T.fail};
  window.upl=1;
  $('fwb').disabled=true;
  $('fws').textContent=T.up+'…';
  x.send(d)};
show($('tab').value);
calc();
