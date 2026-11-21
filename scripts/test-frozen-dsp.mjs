import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const read=f=>fs.readFileSync(path.join(root,f));
const manifest=JSON.parse(read('software/main/generated/manifest.json'));
assert.equal(manifest.schema,4);assert.deepEqual(manifest.boot_parameter_overrides,[]);
const bindings=JSON.parse(read(manifest.binding));
assert.equal(manifest.boot_profile,bindings.profile);
const dir=bindings.compiler_dir+'/';
const image=read('software/main/generated/chime_dsp_image.inc').toString();
const headerText=read('software/main/generated/chime_dsp_parameters.h').toString();
const dataBanks=new Map();
for(const segment of manifest.segments){
 const key=segment.source.split('.')[0].toLowerCase();
 const body=image.match(new RegExp(`image_${key}\\[\\]=\\{([\\s\\S]*?)\\};`))?.[1];assert(body);
 const actual=Buffer.from([...body.matchAll(/0x([0-9a-f]{2})/g)].map(m=>parseInt(m[1],16))).subarray(0,segment.bytes);
 const source=Buffer.from(read(dir+segment.source).toString().replace(/\s/g,''),'hex');
 assert.equal(actual.length,segment.bytes);assert.deepEqual(actual,source);
 assert.match(image,new RegExp(`IMAGE_${key.toUpperCase()}_BYTES ${segment.bytes}U`));
 if(segment.source!=='Program.dat')dataBanks.set(parseInt(segment.address,16),source);
}
function location(address,banks=dataBanks){for(const [base,data] of banks)if(address>=base&&(address-base)*4+4<=data.length)return [data,(address-base)*4];throw Error('outside loaded data');}
for(const ch of ['L','R','SUB']){
 const address=parseInt(manifest.output_gain_addresses[ch],16),[data,offset]=location(address),word=data.readUInt32BE(offset);
 assert.equal(word.toString(16).padStart(8,'0'),manifest.output_gain_words[ch]);
 const signedWord=data.readInt32BE(offset);
 assert.equal(manifest.compiled_gain[ch],signedWord/2**24);
 assert.equal(headerText.match(new RegExp(`CHIME_DSP_GAIN_${ch}_WORD 0x([0-9a-f]+)U`))?.[1],manifest.output_gain_words[ch]);
 assert(Math.abs(manifest.saved_project_gain_linear[ch]*2**24-signedWord)<=1.01);
}
const tmp=path.join(root,'tmp','frozen-dsp-tests');fs.mkdirSync(tmp,{recursive:true});
const fixture=fs.mkdtempSync(path.join(tmp,'case-'));
const outputs=['software/main/generated/manifest.json','software/main/generated/chime_dsp_image.inc','software/main/generated/chime_dsp_parameters.h'];
const files=[...new Set(['scripts/freeze-dsp-image.py',...Object.keys(manifest.source_sha256),...outputs])];
const baseline=new Map(files.map(f=>[f,read(f)]));
function write(f,data){const target=path.join(fixture,f);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,data);}
function reset(){for(const [f,b] of baseline)write(f,b);}
reset();
const python=process.env.PYTHON??'python';
function check(args,ok=true){const r=spawnSync(python,[path.join(fixture,'scripts/freeze-dsp-image.py'),...args],{cwd:fixture,encoding:'utf8',windowsHide:true,env:{...process.env,PYTHONUTF8:'1'}});if(r.error)throw r.error;assert.equal(r.status===0,ok,r.stdout+r.stderr);return r.stdout+r.stderr;}
function json(f){return JSON.parse(fs.readFileSync(path.join(fixture,f)));}
function changeProject(change){const p=json(bindings.project);change(p);write(bindings.project,JSON.stringify(p));}
function walk(o,fn){if(o&&typeof o==='object'){fn(o);Object.values(o).forEach(v=>walk(v,fn));}}
function changeNode(uid,change){changeProject(p=>walk(p,o=>{if(o.Uid===uid&&o.WritableProperties)change(o);}));}
function reject(change,message){reset();change();check(['--update'],false);for(const f of outputs)assert.deepEqual(fs.readFileSync(path.join(fixture,f)),baseline.get(f),message);}
check([]);check(['--update']);check([]);
reject(()=>write(dir+'Program.bin',(baseline.get(dir+'Program.bin')[0]===48?'1':'0')+baseline.get(dir+'Program.bin').toString().slice(1)),'bin/dat mismatch');
reject(()=>write(dir+'Program.dat','abcd'),'word alignment');
reject(()=>write(dir+'Program.dat','00000000'.repeat(8193)),'bank overflow');
reject(()=>write(dir+'PMem.dat','00000000'),'unknown PMem layout');
reject(()=>write(dir+'compiler_output.log','Compile failed: 1 errors'),'compile failure');
reject(()=>write(dir+'ParamAddress.dat',baseline.get(dir+'ParamAddress.dat').toString()+'\n'+bindings.outputs.L.parameter+'\t5\n'),'duplicate symbol');
reject(()=>write(dir+'ParamAddress.dat',baseline.get(dir+'ParamAddress.dat').toString().replace(new RegExp(`^${bindings.outputs.L.parameter.replaceAll('.','\\.')}\\s+\\d+$`,'m'),'')),'missing binding');
reject(()=>write(dir+'ParamAddress.dat',baseline.get(dir+'ParamAddress.dat').toString().replace(new RegExp(`^${bindings.outputs.L.parameter.replaceAll('.','\\.')}\\s+\\d+$`,'m'),bindings.outputs.L.parameter+'\t8191')),'out of loaded data');
reject(()=>changeNode(bindings.outputs.SUB.uid,o=>o.WritableProperties.Gain=o.WritableProperties.IsDB?-2:0.5),'saved/compiled mismatch');
for(const [value,isDb] of [[128,false],[-128.01,false],[10000,true],[null,false]])reject(()=>changeNode(bindings.outputs.SUB.uid,o=>Object.assign(o.WritableProperties,{Gain:value,IsDB:isDb})),'invalid or unencodable gain');
reject(()=>changeNode(bindings.outputs.SUB.uid,o=>o.WritableProperties.NumChannels=2),'multichannel gate');
reject(()=>changeNode(bindings.processor_uid,o=>o.WritableProperties.SERIAL_BYTE_1_0_reg_WORD_LEN='16 bits'),'board format');
reject(()=>changeNode(bindings.outputs.L.uid,o=>o.WritableProperties.FS=96000),'sample rate');
reject(()=>changeProject(p=>walk(p,o=>{if(o.WritableProperties?.SelectedOPChannel==='Ch16')o.WritableProperties.SelectedOPChannel='Ch18';})),'output wiring');
reject(()=>changeProject(p=>walk(p,o=>{if(o.LinkType==='SampleAnalog'&&o.SourceUid===bindings.outputs.L.uid)o.SourceUid=bindings.input.uid;})),'output gate bypass');
reject(()=>changeProject(p=>walk(p,o=>{if(o.LinkType==='SampleAnalog'&&o.TargetUid===bindings.outputs.SUB.uid)o.SourceUid=bindings.outputs.L.uid;})),'shared gate dependency');
reject(()=>{const text=baseline.get(dir+'ParamAddress.dat').toString();const r=manifest.output_gain_addresses.R;write(dir+'ParamAddress.dat',text.replace(new RegExp(`^${bindings.outputs.L.parameter.replaceAll('.','\\.')}\\s+\\d+$`,'m'),bindings.outputs.L.parameter+'\t'+parseInt(r,16)));},'parameter alias');
reject(()=>write(dir+'ParamAddress.dat',baseline.get(dir+'ParamAddress.dat').toString().replace(/^__SafeLoad_Module__\.num_SafeLoad\s+\d+$/m,'')),'partial safeload');
reset();write(outputs[2],baseline.get(outputs[2]).toString().replace(/CHIME_DSP_GAIN_L 0x[0-9a-f]+U/,'CHIME_DSP_GAIN_L 0x0018U'));check([],false);
reset();check(['--source-ref','HEAD','--update'],false);
// Compile real firmware policies against generated maps in a temporary fixture.
// Main production sources are copied, not rewritten, and all variants keep
// targets disjoint across reordered, adjacent, gapped and cross-bank layouts.
const cc=process.env.CC;
const copied=['software/main/bench_dsp.c','software/main/bench_sound.c','software/main/bench_tone.c',
 'software/main/include/bench_dsp.h','software/main/include/bench_registers.inc',
 'software/tests/audio/test_bench_sound.c','software/tests/audio/test_chime_dsp_boot.c','software/main/chime_dsp_boot.c'];
for(const f of copied)write(f,read(f));
const harness=`#include <assert.h>
#include <stdint.h>
#include "bench_dsp.h"
#include "../main/generated/chime_dsp_parameters.h"
static const dsp_memory_region_t map[]={CHIME_DSP_RAM_REGIONS};
static unsigned calls;static uint16_t state=1;static uint64_t clock_us;
static int rd(void *c,uint16_t a,uint8_t *b,size_t n,uint32_t t){(void)c;(void)t;++calls;for(size_t i=0;i<n;++i)b[i]=0;if(a==0xf000){const uint8_t x[]={0,0x60,0,2,0,1,0,1};for(size_t i=0;i<n;++i)b[i]=x[i];}if(a==0xf401)b[1]=2;return 0;}
static int wr(void *c,uint16_t a,const uint8_t *b,size_t n,uint32_t t){(void)c;(void)a;(void)b;(void)n;(void)t;++calls;return 0;}
static uint64_t now(void *c){(void)c;return clock_us;}
static void wait(void *c,uint32_t us){(void)c;clock_us+=us;}
static int status(void *c,uint16_t *s){(void)c;*s=state;return 0;}
int main(void){bench_dsp_t dsp;dsp_transfer_io_t io={.read=rd,.write=wr,.now_us=now,.wait_us=wait};assert(bench_dsp_init(&dsp,io,status));
for(size_t i=0;i<sizeof(map)/sizeof(map[0]);++i){const dsp_memory_region_t *r=map+i;for(uint32_t a=r->first;a<=r->last;++a){uint8_t b[4]={0};sigma_request_t q={.control=SIGMA_CONTROL_WRITE,.address=(uint16_t)a,.data_length=4,.data=b};calls=0;int result=bench_dsp_execute(&dsp,&q,b);if(r->permissions&DSP_REGION_SAFELOAD_TARGET)assert(result==0&&calls==1);else assert(result!=0&&calls==0);}}
const uint16_t gains[]={CHIME_DSP_GAIN_L,CHIME_DSP_GAIN_R,CHIME_DSP_GAIN_SUB};for(unsigned i=0;i<3;++i){uint8_t b[8]={0};sigma_request_t q={.control=SIGMA_CONTROL_WRITE,.flag=1,.address=gains[i],.data_length=4,.data=b};calls=0;int result=bench_dsp_execute(&dsp,&q,b);
#if CHIME_DSP_HAS_SAFELOAD
assert(result==0&&calls==5);
#else
assert(result!=0&&calls==0);
#endif
q.data_length=8;calls=0;result=bench_dsp_execute(&dsp,&q,b);bool adjacent=false;for(size_t j=0;j<sizeof(map)/sizeof(map[0]);++j)if(gains[i]>=map[j].first&&gains[i]<map[j].last&&(map[j].permissions&DSP_REGION_SAFELOAD_TARGET))adjacent=true;
#if CHIME_DSP_HAS_SAFELOAD
if(adjacent)assert(result==0);else assert(result!=0&&calls==0);
#else
(void)adjacent;assert(result!=0&&calls==0);
#endif
}
assert(bench_dsp_init_external(&dsp,io,status));uint8_t b[4]={0};sigma_request_t q={.control=SIGMA_CONTROL_WRITE,.flag=1,.address=CHIME_DSP_GAIN_L,.data_length=4,.data=b};calls=0;assert(bench_dsp_execute(&dsp,&q,b)!=0&&calls==0);return 0;}
`;
write('software/tests/test_generated_policy.c',harness);
function run(cmd,args){const r=spawnSync(cmd,args,{cwd:fixture,encoding:'utf8',windowsHide:true});if(r.error)throw r.error;assert.equal(r.status,0,r.stdout+r.stderr);}
function compileVariant(name){if(!cc)return;const msvc=/^cl(?:\.exe)?$/i.test(path.basename(cc));const build=path.join(fixture,'build',name);fs.mkdirSync(build,{recursive:true});
 const includes=[path.join(fixture,'software/main/include'),path.join(root,'software/components/dsp_control/include'),path.join(root,'software/components/sigma_tcp_bridge/include')];
 const jobs=[['policy',['software/tests/test_generated_policy.c','software/main/bench_dsp.c',path.join(root,'software/components/dsp_control/dsp_transfer.c')],includes],
 ['sound',['software/tests/audio/test_bench_sound.c','software/main/bench_sound.c','software/main/bench_tone.c'],[path.join(root,'software/tests/audio/stubs'),...includes,path.join(root,'software/main/include')]],
 ['boot',['software/tests/audio/test_chime_dsp_boot.c','software/main/chime_dsp_boot.c'],[path.join(root,'software/tests/audio/stubs'),...includes,path.join(root,'software/main/include')]]];
 for(const [label,sources,inc] of jobs){const exe=path.join(build,label+(process.platform==='win32'?'.exe':''));const args=msvc?['/nologo','/std:c11','/W4','/WX',...inc.map(p=>'/I'+p),...sources,'/Fo'+build+path.sep,'/Fe'+exe,'/link','/INCREMENTAL:NO']:['-std=c11','-Wall','-Wextra','-Werror','-Wconversion','-Wsign-conversion',...inc.map(p=>'-I'+p),...sources,'-lm','-o',exe];run(cc,args);run(exe,[]);}}
function updated(){check(['--update']);check([]);return json(outputs[0]);}
function setWordFile(name,offset,word){const f=dir+name;const data=Buffer.from(fs.readFileSync(path.join(fixture,f),'utf8').replace(/\s/g,''),'hex');const n=Math.max(data.length,offset+4);const expanded=Buffer.alloc(n);data.copy(expanded);expanded.writeUInt32BE(word,offset);write(f,expanded.toString('hex'));}
// Both Gain representations, amplification, polarity inversion and zero require matching compiled data.
for(const variant of [{name:'fractional',db:false,values:{L:0.25,R:0.5,SUB:0.125}},{name:'attenuated',db:true,values:{L:-6,R:-12,SUB:-23}},{name:'zero',db:false,values:{L:1,R:0,SUB:0}},{name:'positive-db',db:true,values:{L:1,R:6,SUB:12}},{name:'signed-linear',db:false,values:{L:1.1,R:-0.1,SUB:-2}},{name:'encoding-limits',db:false,values:{L:-128,R:128-2**-24,SUB:0}}]){
 reset();for(const ch of ['L','R','SUB']){const value=variant.values[ch];changeNode(bindings.outputs[ch].uid,o=>Object.assign(o.WritableProperties,{Gain:value,IsDB:variant.db}));const a=parseInt(manifest.output_gain_addresses[ch],16),base=a>=0x6000?0x6000:0;setWordFile(base?'DMem1.dat':'DMem0.dat',(a-base)*4,Math.round((variant.db?10**(value/20):value)*2**24)>>>0);}const imported=updated();for(const ch of ['L','R','SUB']){const linear=variant.db?10**(variant.values[ch]/20):variant.values[ch];assert(Math.abs(imported.compiled_gain[ch]-linear)<=1.01/2**24);assert.match(imported.output_gain_words[ch],/^[0-9a-f]{8}$/);}if(['positive-db','signed-linear','encoding-limits'].includes(variant.name))compileVariant(variant.name);}
for(const [name,addresses] of [['reordered',{L:0x120,R:0x80,SUB:0x100}],['adjacent',{L:0x101,R:0x102,SUB:0x100}],['cross-bank',{L:0x6100,R:0x120,SUB:0x6080}]]){
 reset();let text=baseline.get(dir+'ParamAddress.dat').toString();for(const ch of ['L','R','SUB']){const a=addresses[ch],base=a>=0x6000?0x6000:0;text=text.replace(new RegExp(`^${bindings.outputs[ch].parameter.replaceAll('.','\\.')}\\s+\\d+$`,'m'),bindings.outputs[ch].parameter+'\t'+a);setWordFile(base?'DMem1.dat':'DMem0.dat',(a-base)*4,parseInt(manifest.output_gain_words[ch],16));}write(dir+'ParamAddress.dat',text);const m=updated();for(const ch of ['L','R','SUB'])assert.equal(parseInt(m.output_gain_addresses[ch],16),addresses[ch]);compileVariant(name);
}
reset();write(dir+'ParamAddress.dat',baseline.get(dir+'ParamAddress.dat').toString().replace(/^__SafeLoad_Module__\..*$/gm,''));assert.equal(updated().safeload,null);compileVariant('no-safeload');
reset();let missingOptional=baseline.get(dir+'ParamAddress.dat').toString();for(const symbol of bindings.optional_live_parameters)missingOptional=missingOptional.replace(new RegExp(`^${symbol.replaceAll('.','\\.')}\\s+\\d+$`,'m'),'');write(dir+'ParamAddress.dat',missingOptional);assert.deepEqual(updated().optional_live_parameters,{});compileVariant('no-optional-mixer');
// Move the software staging area independently of the scalar address order.
reset();let text=baseline.get(dir+'ParamAddress.dat').toString();for(const [key,a] of [['data',0x6200],['address',0x6205],['count',0x6206]]){const symbol=bindings.safeload_parameters[key];text=text.replace(new RegExp(`^${symbol.replaceAll('.','\\.')}\\s+\\d+$`,'m'),symbol+'\t'+a);setWordFile('DMem1.dat',(a-0x6000)*4,0);}write(dir+'ParamAddress.dat',text);updated();compileVariant('moved-safeload');
// Upstream block addition/removal, filter frequencies and arbitrary coefficients
// are not graph-pinned. Final physical gate contract is still required.
reset();changeProject(p=>walk(p,o=>{if(o.WritableProperties?.LowFrequency!==undefined){o.WritableProperties.LowFrequency=250;o.WritableProperties.HighFrequency=250;}}));updated();
reset();changeProject(p=>walk(p,o=>{if(o.Uid===bindings.schematic_uid){const old=o.WriteableLinks.find(l=>l.TargetUid===bindings.outputs.SUB.uid);const prev=old.SourceUid;const gain=o.WritableShapes.find(n=>n.Uid===bindings.outputs.SUB.uid);o.WritableShapes.push({...structuredClone(gain),Uid:'TestNewFilter',WritableProperties:{FS:48000,FSOut:48000},PluginName:'TestFilter'});old.SourceUid='TestNewFilter';o.WriteableLinks.push({SourceUid:prev,SourcePinName:'Output0',TargetUid:'TestNewFilter',TargetPinName:'Input',LinkType:'SampleAnalog'});}}));updated();
// Renaming a final gate requires editing the explicit binding, not Python/C.
reset();changeProject(p=>walk(p,o=>{if(o.Uid===bindings.outputs.SUB.uid)o.Uid='SubOutputGate';if(o.SourceUid===bindings.outputs.SUB.uid)o.SourceUid='SubOutputGate';if(o.TargetUid===bindings.outputs.SUB.uid)o.TargetUid='SubOutputGate';}));const renamed=json(manifest.binding);renamed.outputs.SUB.uid='SubOutputGate';renamed.outputs.SUB.parameter='SubOutputGate.Gain';write(manifest.binding,JSON.stringify(renamed));write(dir+'ParamAddress.dat',baseline.get(dir+'ParamAddress.dat').toString().replace(bindings.outputs.SUB.parameter,'SubOutputGate.Gain'));updated();compileVariant('renamed-gate');
// Segment size changes are independent of parameter bindings.
reset();const extra='00000000';write(dir+'Program.dat',baseline.get(dir+'Program.dat').toString().replace(/\s/g,'')+extra);write(dir+'Program.bin',baseline.get(dir+'Program.bin').toString()+'\n'+'0'.repeat(32));write(dir+'DMem0.dat',baseline.get(dir+'DMem0.dat').toString().replace(/\s/g,'')+extra.repeat(32));write(dir+'DMem1.dat',baseline.get(dir+'DMem1.dat').toString().replace(/\s/g,'')+extra.repeat(32));const grown=updated();assert.equal(grown.segments[0].bytes,manifest.segments[0].bytes+4);compileVariant('larger-image');
reset();compileVariant('current');check([]);
console.log('PASS compiler import: dynamic sizes, relocated/reordered/adjacent/cross-bank gains, moved/absent Safeload, signed linear/positive dB/zero/encoding limits, upstream processing changes; binding/board/gate/bounds/mismatch failures preserve prior outputs'+(cc?'; actual C policy and output routing verified across generated variants':'; C variant tests require CC'));
