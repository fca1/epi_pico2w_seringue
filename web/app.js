const SERVICE='7e400001-b5a3-f393-e0a9-e50e24dcca9e';
const COMMAND='7e400002-b5a3-f393-e0a9-e50e24dcca9e';
const STATUS='7e400003-b5a3-f393-e0a9-e50e24dcca9e';
const WIFI_SSID='7e400004-b5a3-f393-e0a9-e50e24dcca9e',WIFI_PASSWORD='7e400005-b5a3-f393-e0a9-e50e24dcca9e';
const WIFI_CONNECT='7e400006-b5a3-f393-e0a9-e50e24dcca9e',WIFI_STATUS='7e400007-b5a3-f393-e0a9-e50e24dcca9e',IP='7e400008-b5a3-f393-e0a9-e50e24dcca9e';
const WIFI_SCAN='7e400009-b5a3-f393-e0a9-e50e24dcca9e',WIFI_RESULTS='7e40000a-b5a3-f393-e0a9-e50e24dcca9e';
let device,command,statusChar,manual=false;
const enc=new TextEncoder(),dec=new TextDecoder();
async function send(value){if(!command)throw Error('Machine non connectée');await command.writeValue(enc.encode(JSON.stringify(value)));}
async function stop(){manual=false;try{await send({command:'stop'});}catch(e){show(e.message);}}
function show(s){document.querySelector('#status').textContent=s;}
document.querySelector('#connect').onclick=async()=>{
 try{device=await navigator.bluetooth.requestDevice({filters:[{namePrefix:'PasteDispenser-',services:[SERVICE]}]});
 device.addEventListener('gattserverdisconnected',()=>{show('Connexion perdue — arrêt local attendu');manual=false;});
 const server=await device.gatt.connect(),service=await server.getPrimaryService(SERVICE);
 command=await service.getCharacteristic(COMMAND);statusChar=await service.getCharacteristic(STATUS);
 await statusChar.startNotifications();statusChar.oncharacteristicvaluechanged=e=>document.querySelector('#telemetry').textContent=dec.decode(e.target.value);
 show('Connectée');}catch(e){show(e.message);}
};
function hold(id,cmd,release){const b=document.querySelector(id);b.onpointerdown=async e=>{e.preventDefault();b.setPointerCapture(e.pointerId);manual=true;await send({command:cmd});};
 ['pointerup','pointercancel','lostpointercapture'].forEach(x=>b.addEventListener(x,()=>{if(manual)send({command:release}).finally(()=>manual=false);}));}
hold('#push','push_start','push_stop');hold('#pull','pull_start','pull_stop');
document.querySelector('#stop').onclick=stop;
document.querySelector('#scanWifi').onclick=async()=>{try{const service=await device.gatt.getPrimaryService(SERVICE);await(await service.getCharacteristic(WIFI_SCAN)).writeValue(Uint8Array.of(1));wifiStatus.textContent='Scan en cours…';const resultsChar=await service.getCharacteristic(WIFI_RESULTS);let result;for(let i=0;i<20;i++){await new Promise(r=>setTimeout(r,500));result=JSON.parse(dec.decode(await resultsChar.readValue()));if(!result.scanning)break;}ssid.innerHTML='<option value="">Sélectionner un réseau</option>';for(const n of result?.networks||[]){const o=document.createElement('option');o.value=n.ssid;o.textContent=`${n.ssid} (${n.rssi} dBm${n.secure?', sécurisé':''})`;ssid.append(o);}wifiStatus.textContent=`${result?.networks?.length||0} réseau(x) trouvé(s)`;}catch(e){wifiStatus.textContent=e.message;}};
document.querySelector('#wifi').onclick=async()=>{try{const service=await device.gatt.getPrimaryService(SERVICE);
 await (await service.getCharacteristic(WIFI_SSID)).writeValue(enc.encode(ssid.value));
 await (await service.getCharacteristic(WIFI_PASSWORD)).writeValue(enc.encode(password.value));
 await (await service.getCharacteristic(WIFI_CONNECT)).writeValue(Uint8Array.of(1));wifiStatus.textContent='Connexion en cours…';
 setTimeout(async()=>{const s=dec.decode(await (await service.getCharacteristic(WIFI_STATUS)).readValue());const ip=dec.decode(await (await service.getCharacteristic(IP)).readValue());wifiStatus.innerHTML=s+(ip?` — <a href="http://${ip}">http://${ip}</a>`:'');},22000);
 }catch(e){wifiStatus.textContent=e.message;}};
document.querySelector('#dose').onclick=()=>send({command:'dose',distance_mm:+distance.value,retract_mm:+retract.value,speed_mm_s:+speed.value}).catch(e=>show(e.message));
sgStart.onclick=()=>send({command:'sg_calibrate_start'});sgFinish.onclick=()=>send({command:'sg_calibrate_finish'});sgCancel.onclick=()=>send({command:'sg_calibrate_cancel'});
window.addEventListener('pagehide',stop);
