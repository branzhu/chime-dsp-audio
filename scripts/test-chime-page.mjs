import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const status={ip:'192.168.4.1',hostname:'chime.local',tcp_address:'192.168.4.1:8086',web_url:'http://192.168.4.1/',bridge_connected:false,station_connected:true,output_mode:7,output_pending:0,output_result:0,requests:0,last_result:0};
const html=fs.readFileSync(new URL('../software/main/bench_index.html',import.meta.url),'utf8');
assert.equal(html.split('{{DEVICE_IP}}').length,2);assert.match(html,/<code id="ip">\{\{DEVICE_IP\}\}<\/code>/);
assert.doesNotMatch(html,/<script[^>]+src=|<link[^>]+href=/i);assert.doesNotMatch(html,/innerHTML|localStorage|sessionStorage/);
assert.match(html,/<select[^>]+id="ssid"/);assert.doesNotMatch(html,/<input[^>]+id="ssid"/);
assert.match(html,/功放数字音量 0 dB，模拟增益 −8 dB/);assert.doesNotMatch(html,/功放固定 −8 dB/);assert.match(html,/已取消固定 1\/8 衰减/);assert.doesNotMatch(html,/USB 输入保留约 −18 dBFS/);assert.doesNotMatch(html,/功放 −20 dB/);
const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
const make=()=>({textContent:'',href:'',value:'',disabled:false,hidden:false,children:[],addEventListener(type,handler){this[type]=handler;},replaceChildren(){this.children=[];this.value='';},append(child){this.children.push(child);},setAttribute(key,value){this[key]=value;}});
const elements=new Map([...html.matchAll(/id="([^"]+)"/g)].map(m=>[m[1],make()]));
let tuningPosted,tuningBehavior='queued';
let outputPosted,outputBehavior='queued';
let interval,fetchCalls=0,mode='ok',gate,posted,soundPosted,dspPosted;const urls=[];
const n={setup:true,online:false,connecting:false,phase:'setup',ssid:'',token:'test-token',error:0};
const nearby=[{id:0,ssid:'<img onerror=alert(1)>',rssi:-40,secure:true,supported:true},{id:1,ssid:'Guest',rssi:-70,secure:false,supported:true},{id:2,ssid:'Enterprise',rssi:-50,secure:true,supported:false}];
let scan={generation:0,scanning:false,error:0,networks:[]};
let dsp={generation:0,present:false,current:false,busy:false,session_connected:false,complete:false,result:0,registers:[]};
const document={hidden:false,getElementById:id=>{assert(elements.has(id),id);return elements.get(id);},createElement:tag=>{assert(['option','tr','td','button','a'].includes(tag));return make();},createElementNS:()=>make()};
const context=vm.createContext({document,AbortController,setTimeout,clearTimeout,setInterval:(fn,ms)=>{assert.equal(ms,2000);interval=fn;},fetch:async(url,options)=>{
  fetchCalls++;urls.push(url);assert.equal(options.cache,'no-store');
  if(mode==='defer')await gate;
  if(mode==='scan-forbidden'&&url.startsWith('/api/scan'))return {ok:false,status:403};
  if(mode==='status-only'&&url==='/api/status')return {ok:false,status:500};
  if(mode==='network')throw new Error('offline');if(mode==='http')return {ok:false,status:500};
  if(url==='/api/dsp/status'){
    if(options.method==='POST'){assert.equal(options.headers['X-Chime-DSP'],'1');assert.equal(options.headers['Content-Type'],'application/json');assert.equal(options.body,'1');dspPosted=options.body;return {ok:true,json:async()=>({queued:true})};}
    assert.equal(options.method,undefined);return {ok:true,json:async()=>structuredClone(dsp)};
  }
  if(url==='/api/tuning'){
    assert.equal(options.method,'POST');assert.equal(options.headers['X-Chime-Tuning'],'1');assert.equal(options.headers['Content-Type'],'application/json');
    assert(['1','2','3','4'].includes(options.body));tuningPosted=options.body;
    return tuningBehavior==='fail'?{ok:false,status:409}:{ok:true,json:async()=>({queued:true})};
  }
  if(url==='/api/sound'){assert.equal(options.method,'POST');assert.equal(options.headers['X-Chime-Sound'],'1');assert.equal(options.headers['Content-Type'],'application/json');soundPosted=options.body;return {ok:true,json:async()=>({queued:true})};}
  if(url==='/api/output'){
    assert.equal(options.method,'POST');assert.equal(options.headers['X-Chime-Output'],'1');assert.equal(options.headers['Content-Type'],'application/json');
    assert(['1','2','3','4','7'].includes(options.body));outputPosted=options.body;
    if(outputBehavior==='fail')return {ok:false,status:409};
    if(outputBehavior==='applied'){status.output_mode=Number(options.body);status.output_pending=0;status.output_result=0;}
    else status.output_pending=Number(options.body);
    return {ok:true,json:async()=>({queued:true})};
  }
  if(url==='/api/wifi'){assert.equal(options.method,'POST');assert.equal(options.headers['X-Chime-Token'],'test-token');posted=JSON.parse(options.body);return {ok:true,json:async()=>({accepted:true})};}
  assert.equal(options.method,undefined);const result=url==='/api/status'?status:url==='/api/network'?n:url.startsWith('/api/scan')?scan:null;assert(result,url);return {ok:true,json:async()=>structuredClone(result)};
}});
vm.runInContext(script,context);await new Promise(resolve=>setImmediate(resolve));
assert.equal(elements.get('ssid').children.length,1,'generation zero must replace static searching placeholder');
assert.match(elements.get('ssid').children[0].textContent,/未找到/);
mode='scan-forbidden';await elements.get('refresh').click();assert.match(elements.get('ssid').children[0].textContent,/无法读取/);assert.match(elements.get('scan-state').textContent,/HTTP 403/);mode='ok';
scan.scanning=true;await elements.get('refresh').click();assert.match(elements.get('ssid').children[0].textContent,/正在扫描/);
scan.scanning=false;scan.error=263;scan.error_name='ESP_ERR_TIMEOUT';await elements.get('refresh').click();assert.match(elements.get('scan-state').textContent,/ESP_ERR_TIMEOUT.*263/);assert.match(elements.get('ssid').children[0].textContent,/扫描失败/);
scan={generation:1,scanning:false,error:0,networks:nearby};mode='status-only';await elements.get('refresh').click();assert.equal(elements.get('ssid').children.length,4,'status/mDNS failure must not block scanned networks');assert.match(elements.get('health').textContent,/状态读取失败/);
mode='ok';await elements.get('refresh').click();
assert(!urls.some(url=>url.includes('refresh=1')),'periodic reads do not trigger RF scan');
await elements.get('scan').click();assert.equal(urls.at(-1),'/api/scan?refresh=1','only explicit rescan requests new RF scan');
assert.equal(elements.get('ip').textContent,'192.168.4.1');assert.equal(elements.get('mdns-url').href,'http://chime.local/');assert.equal(elements.get('ssid').children.length,4);assert.equal(elements.get('ssid').children[1].textContent.startsWith('<img onerror=alert(1)>'),true);assert.equal(elements.get('ssid').children[3].disabled,true);
elements.get('ssid').value='1';elements.get('password').value='secret';elements.get('ssid').change();assert.equal(elements.get('password').disabled,true);assert.equal(elements.get('password').value,'');
elements.get('ssid').value='0';elements.get('ssid').change();elements.get('password').value='password1';await elements.get('wifi-form').submit({preventDefault(){}});assert.deepEqual(posted,{generation:1,id:0,password:'password1'});assert.equal(elements.get('password').value,'');
scan={...scan,generation:8,networks:[{...scan.networks[0],id:3}]};await elements.get('refresh').click();assert.equal(elements.get('ssid').value,'3','preserve SSID not old index');
scan={generation:9,scanning:false,networks:[]};elements.get('password').value='secret';await elements.get('refresh').click();assert.equal(elements.get('ssid').value,'');assert.equal(elements.get('password').value,'');
n.connecting=true;await elements.get('refresh').click();assert.equal(elements.get('connect').disabled,true);n.connecting=false;n.phase='failed';n.error=202;await elements.get('refresh').click();assert.match(elements.get('setup-result').textContent,/失败.*202/);
n.phase='connected';await elements.get('refresh').click();assert.equal(elements.get('setup').hidden,true);assert.equal(elements.get('success').hidden,false);
assert.equal(elements.get('sound-start').disabled,true,'no session cannot play');
assert.equal(soundPosted,undefined,'page load/refresh must never start sound');
assert.match(elements.get('sound-state').textContent,/尚未开始/);
status.bridge_connected=true;await elements.get('refresh').click();
status.bridge_connected=false;await elements.get('refresh').click();
assert.match(elements.get('sound-state').textContent,/尚未开始/,'idle connect/disconnect is not a test');
status.bridge_connected=false;status.dsp_default_ready=true;await elements.get('refresh').click();assert.equal(elements.get('sound-start').disabled,false);
await elements.get('sound-start').click();assert.equal(soundPosted,'3');assert.match(elements.get('sound-state').textContent,/已提交/);
status.sound_attempted=true;status.amplifiers_enabled=true;await elements.get('refresh').click();assert.equal(elements.get('sound-start').disabled,true);assert.match(elements.get('amp-state').textContent,/L\+R\+Sub 播放中/);
await elements.get('sound-stop').click();assert.equal(soundPosted,'0');assert.match(elements.get('sound-state').textContent,/停止/);
status.amplifiers_enabled=false;status.sound_result=259;status.sound_failed_reg=0xf580;await elements.get('refresh').click();assert.match(elements.get('sound-state').textContent,/259.*f580/);
status.bridge_connected=false;status.dsp_default_ready=false;await elements.get('refresh').click();assert.equal(elements.get('sound-start').disabled,true);
assert.match(elements.get('sound-state').textContent,/259.*f580/,'disconnect retains last real test error');
status.sound_result=0;await elements.get('refresh').click();assert.match(elements.get('sound-state').textContent,/当前停止/);
status.sound_attempted=false;status.sound_result=529;await elements.get('refresh').click();
assert.match(elements.get('sound-state').textContent,/529/,'never hide a genuine error based only on attempted flag');
status.dsp_default_ready=true;status.sound_result=0;await elements.get('refresh').click();
assert.equal(elements.get('sound-start').disabled,false,'built-in DSP allows manual sine without SS+');
assert.equal(elements.get('dsp-read').disabled,false,'built-in DSP allows read without SS+');
status.dsp_default_ready=false;await elements.get('refresh').click();
console.log('PASS manual sine UI: no automatic playback, session gating, channel POST/header, active state, stop and failed register');
assert.equal(dspPosted,undefined,'load/polling only reads cache, never requests DSP hardware');
assert.equal(elements.get('dsp-read').disabled,true);assert.match(elements.get('dsp-state').textContent,/尚未读取/);
status.bridge_connected=true;status.amplifiers_enabled=false;await elements.get('refresh').click();
assert.equal(elements.get('dsp-read').disabled,false);await elements.get('dsp-read').click();assert.equal(dspPosted,'1');
dsp={generation:1,present:true,current:true,busy:true,session_connected:true,complete:false,result:0,registers:[]};
await elements.get('refresh').click();assert.equal(elements.get('dsp-read').disabled,true);assert.match(elements.get('dsp-state').textContent,/排队/);
dsp.busy=false;dsp.complete=true;dsp.captured_ms=1000;dsp.result_name='ESP_OK';
dsp.registers=[{address:0xf004,name:'PLL_LOCK',valid:true,value:1},{address:0xf405,name:'CORE_STATUS',valid:true,value:3},{address:0xf580,name:'ASRC_LOCK',valid:true,value:0},{address:0xf050,name:'POWER_ENABLE0',valid:true,value:0x1d71},{address:0xf100,name:'ASRC_INPUT0',valid:true,value:1},{address:0xf140,name:'ASRC_OUT_RATE0',valid:true,value:5}];
await elements.get('refresh').click();assert.match(elements.get('dsp-asrc').textContent,/未锁定.*0000/);assert.match(elements.get('dsp-hint').textContent,/F200.*3.072MHz/);
assert.match(elements.get('dsp-dump').textContent,/0xF580 ASRC_LOCK = 0x0000/);
dsp.registers[3].value=0;await elements.get('refresh').click();assert.match(elements.get('dsp-hint').textContent,/电源未全部启用/);
dsp.registers[3].value=0x1d71;dsp.registers[4].value=0;await elements.get('refresh').click();assert.match(elements.get('dsp-hint').textContent,/F100/);
dsp.registers[4].value=1;dsp.registers[5].value=0;await elements.get('refresh').click();assert.match(elements.get('dsp-hint').textContent,/F140/);
dsp.registers[2].valid=false;dsp.registers[2].error=0;dsp.complete=false;dsp.result=263;dsp.result_name='ESP_ERR_TIMEOUT';
await elements.get('refresh').click();assert.match(elements.get('dsp-asrc').textContent,/未读取/);assert.match(elements.get('dsp-dump').textContent,/ASRC_LOCK = 未读取/);
dsp.current=false;status.bridge_connected=false;await elements.get('refresh').click();assert.match(elements.get('dsp-state').textContent,/快照已过期/);assert.equal(elements.get('dsp-read').disabled,true);
status.bridge_connected=true;status.amplifiers_enabled=true;await elements.get('refresh').click();assert.equal(elements.get('dsp-read').disabled,true);
status.amplifiers_enabled=false;
console.log('PASS DSP UI: manual POST only, cached polling, PLL/core/ASRC decode, valid zero vs unread, configuration hints, stale snapshot and session/playback gating');
for(const error of ['http','network']){mode=error;await elements.get('refresh').click();assert.match(elements.get('health').textContent,/读取失败.*过期/);}
mode='defer';let release;gate=new Promise(resolve=>{release=resolve;});const pending=elements.get('refresh').click();const before=fetchCalls;await elements.get('refresh').click();interval();assert.equal(fetchCalls,before);release();await pending;
document.hidden=true;interval();assert.equal(fetchCalls,before+2);
console.log('PASS page generation-zero/empty/scanning/error states, status-independent scan, no polling rescan, older WebView timeout fallback, live IP/mDNS and XSS-safe SSIDs, open/protected/unsupported APs, POST/token, scan generations, password clearing, connection/success/errors, polling lock and hidden-tab pause');
console.log('Chime web/provisioning host tests passed; real Wi-Fi, mDNS and OS captive popup still require board tests.');

assert.match(html,/启用时恢复内置工程各自的编译增益/);assert.doesNotMatch(html,/内置工程采用 180 Hz|内置工程已有 180 Hz/);assert.doesNotMatch(html,/启用时 DSP Gain 均为 1\.0/);
assert.match(html,/降低主机电平至少 18 dB/);
// Retired UI must not retain calibration inputs, export handlers or calculator.
assert.doesNotMatch(html,/id="(?:cal-|power-)|id="calibration"|calRecords|powerThreshold|sound-channel/);
const options=html.match(/<select id="output-mode">([\s\S]*?)<\/select>/)[1];
assert.match(options,/<option value="7" selected>L\+R\+Sub<\/option>/);
assert.deepEqual([...options.matchAll(/value="(\d+)"/g)].map(m=>m[1]),['1','2','3','4','7']);
mode='ok';document.hidden=false;status.station_connected=true;status.dsp_default_ready=true;status.bridge_connected=false;status.amplifiers_enabled=false;status.sound_result=0;
await elements.get('refresh').click();
const oe=id=>elements.get('output-'+id);
assert.equal(oe('mode').value,'7');assert(oe('apply').disabled);assert.equal(outputPosted,undefined);
const outputCalls=()=>urls.filter(u=>u==='/api/output').length;
const noWrite=outputCalls();oe('mode').value='4';oe('mode').change();
assert.equal(outputCalls(),noWrite,'changing selection never sends a hardware request');
assert.match(oe('current').textContent,/L\+R/);assert(!oe('apply').disabled);assert(elements.get('sound-start').disabled,'apply the draft before starting tone');
await oe('apply').click();assert.equal(outputPosted,'4');assert.equal(soundPosted,'0','output selection never starts a tone');
assert(oe('apply').disabled&&oe('mode').disabled);assert.match(oe('current').textContent,/正在切换至 Sub/);
assert.equal(status.output_mode,7,'queued request is not yet applied');
status.output_pending=0;status.output_mode=4;status.output_result=0;await elements.get('refresh').click();
assert.match(oe('current').textContent,/当前已应用：Sub/);assert.match(oe('state').textContent,/已应用 Sub/);assert.equal(oe('mode').value,'4');
for(const mask of [1,2,3,7]){
  status.amplifiers_enabled=true;outputBehavior='applied';
  oe('mode').value=String(mask);oe('mode').change();assert(!oe('apply').disabled,'USB playback allows explicit switch');
  await oe('apply').click();assert.equal(outputPosted,String(mask));assert.match(oe('state').textContent,/已应用/);
  assert.equal(status.output_mode,mask);assert(oe('apply').disabled);
}
status.amplifiers_enabled=false;outputBehavior='queued';oe('mode').value='4';oe('mode').change();await oe('apply').click();
status.output_pending=0;status.output_result=263;await elements.get('refresh').click();
assert.match(oe('state').textContent,/未成功.*263/);assert.match(oe('current').textContent,/L\+R\+Sub/);assert(!oe('apply').disabled);
outputBehavior='fail';await oe('apply').click();assert.match(oe('state').textContent,/HTTP 409/);assert(!oe('apply').disabled);
for(const patch of [{bridge_connected:true},{dsp_default_ready:false},{station_connected:false}]){
  const saved={...status};Object.assign(status,patch);await elements.get('refresh').click();assert(oe('mode').disabled&&oe('apply').disabled);Object.assign(status,saved);
}
status.output_pending=2;await elements.get('refresh').click();assert(oe('apply').disabled&&oe('mode').disabled);status.output_pending=0;
mode='status-only';await elements.get('refresh').click();assert(oe('mode').disabled&&oe('apply').disabled&&elements.get('sound-start').disabled);assert.match(oe('current').textContent,/未确认/);
mode='ok';delete status.output_mode;await elements.get('refresh').click();assert(oe('apply').disabled);assert.match(oe('current').textContent,/升级/);
status.output_mode=3;status.output_result=0;await elements.get('refresh').click();await oe('stop').click();assert.equal(soundPosted,'0');
const outputFinal=outputCalls();interval();await new Promise(resolve=>setImmediate(resolve));assert.equal(outputCalls(),outputFinal,'polling never reapplies outputs');
console.log('PASS retired calibration UI + output switch: exact five modes, manual apply only, applied vs draft/queued/failure, USB switch enabled, SS+/offline/old-firmware guarded, STOP and no automatic tone/retry');
const evidence=JSON.parse(fs.readFileSync(new URL('../software/tests/fixtures/calibration/lr-unity-left.json',import.meta.url),'utf8'));
assert.equal(evidence.status,'user_accepted');assert.deepEqual(evidence.measurements.map(r=>r.output_rms_mv),[84,248,438,774,1380,2475,4395,5036,5648,7985]);
console.log('PASS accepted calibration evidence preserved outside retired UI');

// SS audition is explicit, RAM-retained and never invokes built-in output writes.
mode='ok';status.station_connected=true;status.dsp_default_ready=false;status.bridge_connected=true;
status.dsp_external=true;status.tuning_armed=0;status.tuning_pending=0;status.tuning_result=0;
await elements.get('refresh').click();assert.equal(tuningPosted,undefined,'polling must not arm audition');
assert.equal(elements.get('tuning-start').disabled,false);assert.equal(elements.get('output-apply').disabled,true);assert.equal(elements.get('sound-start').disabled,true);
elements.get('tuning-banks').value='2';await elements.get('tuning-start').click();assert.equal(tuningPosted,'2');assert.match(elements.get('tuning-state').textContent,/已提交/);
status.tuning_pending=2;await elements.get('refresh').click();assert.equal(elements.get('tuning-start').disabled,true);assert.match(elements.get('tuning-state').textContent,/排队/);
status.tuning_pending=0;status.tuning_armed=4;await elements.get('refresh').click();assert.match(elements.get('tuning-state').textContent,/已授权.*Sub/);
await elements.get('tuning-stop').click();assert.equal(soundPosted,'0');
status.tuning_armed=0;status.bridge_connected=false;await elements.get('refresh').click();assert.equal(elements.get('tuning-start').disabled,false);assert.match(elements.get('bridge').textContent,/RAM 保留/);
await elements.get('tuning-restore').click();assert.equal(tuningPosted,'4');
status.tuning_result=259;await elements.get('refresh').click();assert.match(elements.get('tuning-state').textContent,/259/);
tuningBehavior='fail';await elements.get('tuning-start').click();assert.match(elements.get('tuning-state').textContent,/409/);tuningBehavior='queued';
mode='status-only';await elements.get('refresh').click();assert.equal(elements.get('tuning-start').disabled,true);assert.equal(elements.get('tuning-restore').disabled,true);
mode='ok';status.dsp_external=false;status.dsp_default_ready=true;status.tuning_result=0;await elements.get('refresh').click();assert.equal(elements.get('tuning-start').disabled,true);assert.match(elements.get('tuning-state').textContent,/内置/);
console.log('PASS SS audition UI: no automatic arm/restore, bank selection, queued/applied/error, retained RAM after disconnect, external output/tone disabled, manual STOP and offline guards');

status.selected_source='bluetooth';status.rendering_source='none';status.bt_available=true;status.bt_connected=true;status.bt_playing=false;status.source_stopped=true;
status.dsp_default_ready=true;status.dsp_external=false;status.bridge_connected=false;status.amplifiers_enabled=false;
await elements.get('refresh').click();
assert.match(elements.get('source-state').textContent,/选中 蓝牙.*输出 无.*连接\/暂停/);
status.bt_playing=true;status.source_stopped=false;status.rendering_source='bluetooth';
for(const [state,label] of [[1,'缓冲中'],[4,'等待音频恢复'],[2,'播放'],[5,'音频故障']]){
 status.bt_stream_state=state;await elements.get('refresh').click();
 assert.ok(elements.get('source-state').textContent.includes(`连接/${label}`));
}
status.bt_playing=false;status.source_stopped=true;status.rendering_source='none';status.bt_stream_state=0;
status.bt_codec='AAC';status.bt_rate=44100;await elements.get('refresh').click();
assert.match(elements.get('source-state').textContent,/AAC 44100 Hz/);
for(const codec of ['aptX','aptX HD','Opus','LDAC','LHDC V5']){
 status.bt_codec=codec;status.bt_rate=48000;await elements.get('refresh').click();
 assert.ok(elements.get('source-state').textContent.includes(`${codec} 48000 Hz`));
}
status.bt_codec='SBC';status.bt_rate=48000;await elements.get('refresh').click();
assert.match(elements.get('source-state').textContent,/SBC 48000 Hz/);
assert.equal(elements.get('source-resume').disabled,false);
await elements.get('source-resume').click();assert.equal(soundPosted,JSON.stringify('resume'));
await elements.get('source-stop').click();assert.equal(soundPosted,'0');
console.log('PASS Bluetooth selected/rendering distinction and manual resume/STOP controls');
