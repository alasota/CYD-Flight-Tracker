// After a successful update: wait for the board to come back, then reload.
setTimeout(function p(){fetch('/',{cache:'no-store'}).then(function(r){
  if(r.ok||r.status==401)location.href='/?tab=firmware';else setTimeout(p,2000)})
  .catch(function(){setTimeout(p,2000)})},5000);
