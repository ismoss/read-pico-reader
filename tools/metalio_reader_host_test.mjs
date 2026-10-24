/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：回归 S31 读取帧流控、结束帧校验、连续命令、截断与超时保护。
 * English: Regress S31 read-frame flow control, final-digest validation, consecutive commands, truncation and timeouts.
 */
import assert from 'node:assert/strict';import {createHash,randomBytes} from 'node:crypto';import fs from 'node:fs';
import {readS31Flash} from '../flash/metalio-flash-reader.js';
function fixture(data,options={}){
  let packets=[],received=0,commands=0,digests=0;
  const loader={chip:{IMAGE_CHIP_ID:32},IS_STUB:true,ESP_READ_FLASH:0xd2,
    async checkCommand(_label,op,request){
      assert.equal(packets.length,0,'previous final digest must be consumed before a new command');assert.equal(op,0xd2);
      const v=new DataView(request.buffer,request.byteOffset,request.byteLength);assert.equal(v.getUint32(4,true),data.length);assert.equal(v.getUint32(8,true),1024);assert.equal(v.getUint32(12,true),4);
      commands++;received=0;
      for(let at=0;at<data.length;at+=1024)packets.push(new Uint8Array(data.slice(at,at+1024)));
      if(options.short)packets[0]=packets[0].slice(0,packets[0].length-1);
      if(!options.missingDigest){const digest=new Uint8Array(createHash('md5').update(data).digest());if(options.corrupt)digest[0]^=1;packets.push(digest);}
      return options.reject?1:0;
    },
    transport:{
      async read(){const value=packets.shift();if(!value)return new Promise(()=>{});if(received===data.length)digests++;return value;},
      async write(ack){assert.equal(ack.length,4);received+=Math.min(1024,data.length-received);assert.equal(new DataView(ack.buffer).getUint32(0,true),received);}
    }};
  return {loader,get commands(){return commands;},get digests(){return digests;}};
}
const data=randomBytes(32768+91),f=fixture(data);let done=0;
assert.deepEqual(await readS31Flash(f.loader,0x8000,data.length,n=>done=n),new Uint8Array(data));assert.equal(done,data.length);assert.equal(f.digests,1);
assert.deepEqual(await readS31Flash(f.loader,0x10000,data.length),new Uint8Array(data));assert.equal(f.commands,2);assert.equal(f.digests,2);
for(const [options,pattern]of [[{short:true},/长度异常/],[{corrupt:true},/MD5/],[{reject:true},/拒绝/]]){
 const bad=fixture(data,options);await assert.rejects(readS31Flash(bad.loader,0x8000,data.length),pattern);
 await assert.rejects(readS31Flash(bad.loader,0x8000,data.length),/会话已中断/);assert.equal(bad.commands,1);
}
const missing=fixture(data,{missingDigest:true});await assert.rejects(readS31Flash(missing.loader,0x8000,data.length,()=>{},10),/超时/);
const big=fixture(Buffer.alloc(5*1024*1024,0xa5));assert.equal((await readS31Flash(big.loader,0x10000,5*1024*1024)).length,5*1024*1024);assert.equal(big.digests,1);
await assert.rejects(readS31Flash(f.loader,0xffffff,2),/范围/);
const installer=fs.readFileSync(new URL('../flash/metalio-installer.js',import.meta.url),'utf8');
assert.match(installer,/serialOptions:\{bufferSize:65536\}/);assert(!installer.includes('loader.readFlash('),'all installation reads must use the verified reader');
console.log('PASS: bounded in-flight frames, cumulative ACKs, final MD5 consumption, sequential reads, exact large backup, corruption/truncation/timeout stop and Web Serial buffer sizing');
