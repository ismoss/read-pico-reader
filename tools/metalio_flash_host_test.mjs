/* 中文：对真实 S31 分区、启动记录及跨设备快照执行失败保护测试。 / English: Exercise real S31 layout/boot guards and cross-device snapshot failures. */
import fs from 'node:fs';import vm from 'node:vm';import assert from 'node:assert/strict';import {webcrypto,createHash} from 'node:crypto';
import {equal,identity,partitionTable,selectedSlot,otaCRC} from '../flash/metalio-flash-core.js';
const site=process.argv[2];assert(site,'Pass the staged site directory');
const manifest=JSON.parse(fs.readFileSync(`${site}/metalio/manifest.json`));
const app=fs.readFileSync(`${site}/metalio/firmware.bin`),table=fs.readFileSync(`${site}/metalio/partitions.bin`);
assert.equal(identity(app),manifest.version);assert(partitionTable(table));
const wrong=Buffer.from(app);wrong.writeUInt16LE(9,12);assert.throws(()=>identity(wrong));
const foreign=Buffer.from(app);foreign.write('Other',80);assert.throws(()=>identity(foreign));
const corrupt=Buffer.from(table);corrupt[4]^=1;assert.throws(()=>partitionTable(corrupt));
let data=new Uint8Array(8192).fill(255);assert.equal(selectedSlot(data).address,0x10000);
function entry(at,sequence,state){const v=new DataView(data.buffer);v.setUint32(at,sequence,true);v.setUint32(at+24,state,true);v.setUint32(at+28,otaCRC(data.slice(at,at+4)),true);}
entry(0,2,2);assert.equal(selectedSlot(data).address,0x510000);
entry(4096,3,2);assert.equal(selectedSlot(data).address,0x10000);
entry(4096,3,1);assert.throws(()=>selectedSlot(data),/尚未确认/);
entry(4096,3,2);data[4096+28]^=1;assert.throws(()=>selectedSlot(data),/校验失败/);
assert.equal(otaCRC(new Uint8Array([1,0,0,0])),0x4743989a);
let fault=false,live=new Set();class U extends URL{static createObjectURL(b){const u=URL.createObjectURL(b);live.add(u);return u;}static revokeObjectURL(u){assert(live.delete(u));URL.revokeObjectURL(u);}}
const ctx=vm.createContext({window:{},crypto:webcrypto,Uint8Array,DataView,TextDecoder,Blob,URL:U,fetch:async(url,options)=>{
 assert.equal(options.cache,'no-store');const u=new URL(url);let name=u.pathname.split('/').pop();if(name.startsWith('Metalio-S31-update-'))name='firmware.bin';
 let bytes=fs.readFileSync(`${site}/metalio/${name}`);if(fault&&name==='firmware.bin'){bytes=Buffer.from(bytes);bytes[400]^=1;}return new Response(bytes);
}});vm.runInContext(fs.readFileSync('flash/flasher-integrity.js','utf8'),ctx);
const api=ctx.window.KiikoreadFlasher;
assert.throws(()=>api.validate(manifest,'pico'),/所选设备/);
const pico=JSON.parse(fs.readFileSync('flash/manifest.json'));assert.throws(()=>api.validate(pico,'metalio'),/所选设备/);
const invalid=structuredClone(manifest);invalid.builds[0].parts[0].offset=0;assert.throws(()=>api.validate(invalid,'metalio'),/地址/);
const snapshot=await api.prepare(manifest,'https://kiikoread.com/metalio/manifest.json',undefined,'metalio');assert.equal(snapshot.parts.length,4);assert.equal(live.size,5);snapshot.dispose();assert.equal(live.size,0);
fault=true;await assert.rejects(api.prepare(manifest,'https://kiikoread.com/metalio/manifest.json',undefined,'metalio'),/校验失败/);assert.equal(live.size,0);
const hashes={};for(const p of manifest.builds[0].parts)hashes[p.path]=createHash('sha256').update(fs.readFileSync(`${site}/metalio/${p.path}`)).digest('hex');
assert(manifest.builds[0].parts.every(p=>hashes[p.path]===p.sha256));
console.log('PASS: S31 real bundle, chip/project/partition guards, OTA CRC/confirmed slot, corruption rejection, snapshot lifetime, and cross-device refusal');
