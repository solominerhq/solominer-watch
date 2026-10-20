(() => {
'use strict';
const root=document.getElementById('sm-phone-setup');
const $=s=>root.querySelector(s);
const form=$('#sm-setup-form');
const fields={network:$('#sm-network'),ssid:$('#sm-ssid'),password:$('#sm-password'),address:$('#sm-address'),timezone:$('#sm-timezone')};
let config=null,networks=[],scanning=false,scanTimer=0,scanAbort=null,scanRound=0,saving=false,hotspot=false,uploading=false;
let view=location.pathname==='/update'?'update':'setup';
let savedName='your Wi-Fi';
const aliases={'US/Pacific':'America/Los_Angeles','US/Eastern':'America/New_York','US/Central':'America/Chicago','US/Mountain':'America/Denver','Asia/Calcutta':'Asia/Kolkata','Asia/Katmandu':'Asia/Kathmandu','Europe/Dublin':'Europe/London','Europe/Paris':'Europe/Berlin','Europe/Madrid':'Europe/Berlin','Europe/Rome':'Europe/Berlin','Europe/Amsterdam':'Europe/Berlin','Europe/Helsinki':'Europe/Athens','Asia/Singapore':'Asia/Shanghai','Asia/Hong_Kong':'Asia/Shanghai','Asia/Seoul':'Asia/Tokyo','Australia/Melbourne':'Australia/Sydney','Asia/Jakarta':'Asia/Bangkok','America/Buenos_Aires':'America/Argentina/Buenos_Aires','Etc/UTC':'UTC'};
function text(el,value){el.textContent=value;}
function announce(s){text($('#sm-announcement'),s);}
function error(name,message){const el=$('#sm-'+name+'-error');if(el){text(el,message);el.hidden=!message;}if(fields[name])fields[name].setAttribute('aria-invalid',String(!!message));}
function stopScan(){clearTimeout(scanTimer);if(scanAbort)scanAbort.abort();scanAbort=null;scanning=false;scanRound++;$('#sm-refresh').disabled=false;}
function setView(next){view=next;form.hidden=view!=='setup';$('#sm-handoff').hidden=view!=='saved';$('#sm-update-view').hidden=view!=='update';$('.sm-header-copy').hidden=view!=='setup';$('.sm-footer').hidden=view==='update';if(view!=='setup')stopScan();text($('#sm-saved-network'),savedName);text($('#sm-handoff-note-text'),hotspot?'Using this phone’s hotspot? Turn it back on now so your watch can connect.':'This setup page may close when your watch switches networks. That’s expected.');root.scrollIntoView({block:'start'});}
function inputBusy(busy){form.querySelectorAll('input,textarea,select,button').forEach(el=>{el.disabled=busy;});if(!busy)passwordState();}
function networkName(){return fields.network.value==='manual'?fields.ssid.value:(fields.network.value.startsWith('n:')?fields.network.value.slice(2):'');}
function passwordState(){const same=config&&networkName()===config.ssid;const open=$('#sm-open-network').checked;fields.password.disabled=open;$('#sm-password-toggle').disabled=open;fields.password.placeholder=open?'No password needed':same&&config.hasPassword?'Saved password · leave blank to keep':'Network password';}
function networkChanged(){const manual=fields.network.value==='manual';$('#sm-manual-wrap').hidden=!manual;fields.ssid.required=manual;const selected=networks.find(n=>n.ssid===networkName());hotspot=false;$('#sm-open-network').checked=!manual&&(selected?selected.open:!!(config&&config.ssid&&networkName()===config.ssid&&!config.hasPassword));fields.password.value='';passwordState();error('network','');error('password','');}
function populateNetworks(){const selected=fields.network.value;fields.network.replaceChildren(new Option('Select Wi-Fi',''));const names=new Set();function add(name,label){if(!name||names.has(name))return;names.add(name);fields.network.add(new Option(label||name,'n:'+name));}if(config)add(config.ssid,config.ssid);networks.slice().sort((a,b)=>b.rssi-a.rssi).forEach(n=>add(n.ssid,n.ssid+(n.open?' · Open':'')));if(selected&&selected!=='manual')add(selected.slice(2),selected.slice(2));fields.network.add(new Option('Other network…','manual'));fields.network.value=selected;}
async function scan(refresh=false){
 if(saving||uploading||view!=='setup'||!config)return;
 stopScan();const round=scanRound;scanning=true;$('#sm-refresh').disabled=true;text($('#sm-network-hint'),'Looking for nearby networks…');
 const start=Date.now();
 async function poll(first){
  scanTimer=0;
  if(round!==scanRound||view!=='setup')return;
  scanAbort=new AbortController();const timer=setTimeout(()=>scanAbort&&scanAbort.abort(),9000);
  try{
   const response=await fetch('/api/networks'+(first&&refresh?'?refresh=1':''),{cache:'no-store',signal:scanAbort.signal});
   const data=await response.json();if(!response.ok||data.error)throw new Error(data.error||data.message||'Unable to scan networks.');
   if(round!==scanRound)return;
   if(data.scanning){if(Date.now()-start>18000)throw new Error('Scan took too long. Try again or enter the network manually.');scanTimer=setTimeout(()=>poll(false),900);return;}
   networks=data.networks||[];populateNetworks();text($('#sm-network-hint'),networks.length?'2.4 GHz networks · choose yours or enter it manually.':'No networks found. Turn on your hotspot or enter its name manually.');announce(networks.length+' Wi-Fi networks found.');
  }catch(e){if(round===scanRound)text($('#sm-network-hint'),e.name==='AbortError'?'Scan timed out. Enter the network manually or try again.':e.message);}
  finally{clearTimeout(timer);if(round===scanRound&&!scanTimer){scanning=false;$('#sm-refresh').disabled=false;}if(round===scanRound&&scanTimer&&Date.now()-start>18000){clearTimeout(scanTimer);scanTimer=0;scanning=false;$('#sm-refresh').disabled=false;}}
 }
 // A completed response clears the poll marker so the refresh control recovers.
 await poll(true);
}
function clockPreview(){
 if(!config)return;
 const value=fields.timezone.value,zone=config.zones.find(z=>z.value===value),hour12=$('input[name="sm-format"]:checked').value==='12';
 const display=$('#sm-time-preview');display.replaceChildren();
 if(!zone){text(display,'—:—');return;}
 let now=new Date(),timeZone=zone.iana||'UTC';if(Number.isFinite(zone.offset))now=new Date(now.getTime()+zone.offset*3600000);
 try{const pieces=new Intl.DateTimeFormat('en-US',{hour:'numeric',minute:'2-digit',hour12,timeZone}).formatToParts(now);pieces.forEach(p=>{if(p.type==='dayPeriod'){const small=document.createElement('small');text(small,p.value);display.append(small);}else display.append(document.createTextNode(p.value));});}catch(e){text(display,'—:—');}
}
async function load(){
 inputBusy(true);$('#sm-load-message').hidden=false;text($('#sm-load-message'),'Loading your watch’s settings…');$('#sm-load-retry').hidden=true;
 const abort=new AbortController(),timer=setTimeout(()=>abort.abort(),10000);
 try{
  const response=await fetch('/api/setup',{cache:'no-store',signal:abort.signal});const data=await response.json();if(!response.ok)throw new Error(data.message||'Settings could not be loaded.');
  config=data;root.querySelectorAll('[data-firmware-version]').forEach(el=>text(el,data.version));
  fields.network.value='';populateNetworks();fields.network.value=data.ssid?'n:'+data.ssid:'';fields.password.value='';$('#sm-open-network').checked=!!data.ssid&&!data.hasPassword;
  fields.address.value=data.address||'';$('#sm-worker').value=data.worker;$('#sm-pool').value=data.pool;$('#sm-port').value=data.port;$('#sm-pool-password').value=data.poolPassword;
  fields.timezone.replaceChildren(new Option('Choose your timezone',''));data.zones.forEach(z=>fields.timezone.add(new Option(z.label,z.value)));
  if(data.timezone){if(!data.zones.some(z=>z.value===data.timezone))fields.timezone.add(new Option('Saved timezone · '+data.timezone,data.timezone));fields.timezone.value=data.timezone;text($('#sm-timezone-hint'),'Your saved timezone. Change it if needed.');}
  else{let detected='';try{detected=Intl.DateTimeFormat().resolvedOptions().timeZone;}catch(e){}detected=aliases[detected]||detected;const suggested=data.zones.find(z=>z.iana===detected)||(detected==='UTC'?data.zones.find(z=>z.value==='UTC0'):null);fields.timezone.value=suggested?suggested.value:'';text($('#sm-timezone-hint'),suggested?'Suggested from your phone. Change it if needed.':'Choose the region where you’ll wear your watch.');}
  $('input[name="sm-format"][value="'+(data.clockFormat===24?'24':'12')+'"]').checked=true;
  $('#sm-load-message').hidden=true;inputBusy(false);networkChanged();clockPreview();setView(view);if(view==='setup')scan();
 }catch(e){text($('#sm-load-message'),'Couldn’t reach your watch. Stay connected to its SOLOMINER Wi-Fi and try again.');$('#sm-load-retry').hidden=false;}
 finally{clearTimeout(timer);}
}
function validAddress(value){
 if(/^[13][1-9A-HJ-NP-Za-km-z]{25,34}$/.test(value))return true;
 if(value!==value.toLowerCase()&&value!==value.toUpperCase())return false;
 const a=value.toLowerCase(),b='qpzry9x8gf2tvdw0s3jn54khce6mua7l';if(!a.startsWith('bc1')||a.length<14||a.length>79)return false;
 let c=1;const values=[3,3,0,2,3],g=[0x3b6a57b2,0x26508e6d,0x1ea119fa,0x3d4233dd,0x2a1462b3];for(let i=3;i<a.length;i++){const d=b.indexOf(a[i]);if(d<0)return false;values.push(d);}for(const n of values){const t=c>>>25;c=((c&0x1ffffff)<<5)^n;for(let j=0;j<5;j++)if((t>>j)&1)c^=g[j];}const v=b.indexOf(a[3]);return v===0?(c>>>0)===1:v>=1&&v<=16&&(c>>>0)===0x2bc830a3;
}
async function save(event){
 event.preventDefault();if(saving||!config)return;error('save','');let first=null;
 function bad(name,message,target){error(name,message);if(message&&!first)first=target||fields[name];}
 const ssid=networkName(),pass=fields.password.value,open=$('#sm-open-network').checked;
 const keep=ssid===config.ssid&&config.hasPassword&&!pass&&!open;
 bad('network',!ssid?'Choose a network or enter its name.':new TextEncoder().encode(ssid).length>32?'Network names must fit within 32 bytes.':'',fields.network.value==='manual'?fields.ssid:fields.network);
 bad('password',!open&&!keep&&!((pass.length>=8&&pass.length<=63)||/^[0-9a-fA-F]{64}$/.test(pass))?'Check your Wi-Fi password (usually 8–63 characters).':'');
 const address=fields.address.value.trim();bad('address',!address?'Add your Bitcoin receiving address.':!validAddress(address)?'That address doesn’t look right. Copy it again from your wallet.':'');
 bad('timezone',fields.timezone.value?'':'Choose your timezone.');
 const port=Number($('#sm-port').value),pool=$('#sm-pool').value.trim();const invalidPool=!pool||/[\s/\\]/.test(pool)||!Number.isInteger(port)||port<1||port>65535;
 if(invalidPool)$('#sm-advanced').open=true;bad('advanced',invalidPool?'Check the pool host and port (1–65535).':'',$('#sm-pool'));
 if(first){first.focus();return;}
 const body=new URLSearchParams({token:config.token,s:ssid,p:pass,open:open?'1':'0',keepPassword:keep?'1':'0',btcAddress:address,PosixTZ:fields.timezone.value,ClockFormat:$('input[name="sm-format"]:checked').value,WorkerName:$('#sm-worker').value.trim(),Poolurl:pool,Poolport:String(port),Poolpassword:$('#sm-pool-password').value});
 stopScan();saving=true;inputBusy(true);text($('#sm-save'),'Saving settings…');
 const abort=new AbortController(),timer=setTimeout(()=>abort.abort(),15000);
 try{
  const response=await fetch('/wifisave',{method:'POST',body,signal:abort.signal,cache:'no-store'});const data=await response.json();
  if(!response.ok||!data.ok){const target=data.field||'save';error(target,data.message||'Could not save settings.');if(target==='advanced')$('#sm-advanced').open=true;inputBusy(false);if(fields[target])fields[target].focus();return;}
  // Only an acknowledged, durable save can show this state.
  savedName=ssid;config.ssid=ssid;config.hasPassword=!open;fields.password.value='';setView('saved');announce('Settings saved. Check your watch as it connects.');
 }catch(e){error('save','Couldn’t confirm the save. Check your watch. If it is still in Setup, reconnect to its Wi-Fi and try again.');}
 finally{clearTimeout(timer);saving=false;inputBusy(false);$('#sm-save').innerHTML='Save &amp; connect <span aria-hidden="true">→</span>';}
}
fields.network.addEventListener('change',networkChanged);
fields.ssid.addEventListener('input',()=>{error('network','');passwordState();});
$('#sm-password-toggle').addEventListener('click',()=>{const show=fields.password.type==='password';fields.password.type=show?'text':'password';text($('#sm-password-toggle'),show?'Hide':'Show');$('#sm-password-toggle').setAttribute('aria-pressed',String(show));});
$('#sm-open-network').addEventListener('change',()=>{passwordState();error('password','');});
$('#sm-use-hotspot').addEventListener('click',()=>{fields.network.value='manual';networkChanged();hotspot=true;$('#sm-open-network').checked=false;passwordState();fields.ssid.placeholder='Your phone’s hotspot name';fields.ssid.focus();});
$('#sm-refresh').addEventListener('click',()=>scan(true));
for(const name of ['address','password'])fields[name].addEventListener('input',()=>error(name,''));
fields.timezone.addEventListener('change',()=>{error('timezone','');text($('#sm-timezone-hint'),'Your watch will use this timezone.');clockPreview();});
root.querySelectorAll('input[name="sm-format"]').forEach(el=>el.addEventListener('change',clockPreview));
form.addEventListener('submit',save);$('#sm-save').addEventListener('click',save);
$('#sm-load-retry').addEventListener('click',load);
$('#sm-edit').addEventListener('click',()=>{setView('setup');passwordState();});
$('#sm-open-update').addEventListener('click',()=>setView('update'));
$('#sm-update-back').addEventListener('click',()=>{if(!uploading)setView('setup');});
$('#sm-firmware-file').addEventListener('change',e=>{const f=e.target.files[0],valid=!!f&&f.size>0&&f.name.toLowerCase().endsWith('.bin');$('#sm-update-action').disabled=!valid;text($('#sm-update-message'),f&&!valid?'Choose a non-empty .bin firmware file.':'');});
$('#sm-update-action').addEventListener('click',()=>{
 const f=$('#sm-firmware-file').files[0];if(!f||uploading)return;
 uploading=true;stopScan();$('#sm-update-action').disabled=true;$('#sm-update-back').disabled=true;$('#sm-firmware-file').disabled=true;
 const xhr=new XMLHttpRequest(),body=new FormData();body.append('update',f);xhr.open('POST','/u');xhr.timeout=180000;
 text($('#sm-update-message'),'Uploading firmware… Keep your watch plugged in.');
 xhr.upload.onprogress=e=>{if(e.lengthComputable)text($('#sm-update-message'),'Uploading · '+Math.round(e.loaded/e.total*100)+'%');};
 function finish(message,success){text($('#sm-update-message'),message);uploading=false;$('#sm-update-back').disabled=false;$('#sm-firmware-file').disabled=success;$('#sm-update-action').disabled=success;}
 xhr.onload=()=>{const success=xhr.status===200&&/Update successful\./i.test(xhr.responseText);finish(success?'Update installed. Your watch is restarting.':'The watch could not install this file. Check that it is the SOLOMINER application firmware and try again.',success);};
 xhr.onerror=xhr.ontimeout=()=>finish('Connection lost before the update was confirmed. Check the watch before trying again.',false);
 xhr.send(body);
});
setView(view);load();
})();
