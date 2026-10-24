/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：以真实 Espressif 复位实现回归 S31 握手；覆盖不复位、跨芯片和超时停止。
 * English: Regress S31 handshakes with Espressif's actual reset implementation, including no-reset, wrong-chip and timeout failures.
 */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {ESPLoader} from '../flash/vendor/metalio/esptool-js-0.7.0.js';
import {connectS31,connectionMessage} from '../flash/metalio-connection.js';

const expected=['RTS:false','DTR:false','DTR:true','RTS:false','RTS:true','DTR:false','RTS:true','RTS:false','DTR:false'];
function fixture(){
  const signals=[];
  const transport={getInfo:()=>'',getVid:()=>0x303a,getPid:()=>0x1001,
    connect:async()=>{},readLoop(){},peek:()=>new Uint8Array(),flushInput(){},
    setRTS:async value=>signals.push(`RTS:${value}`),setDTR:async value=>signals.push(`DTR:${value}`)};
  const loader=new ESPLoader({transport,baudrate:115200,romBaudrate:115200,terminal:{clean(){},write(){},writeLine(){}}});
  const registers=[];loader.writeReg=async(address,value)=>registers.push([address,value]);loader.readReg=async address=>{assert.equal(address,0x2080101c);return 0x1234;};
  // 仅模拟 ROM 应答和芯片描述；连接/尝试/USB 复位执行原厂代码。
  // Simulate only ROM replies and chip description; retain upstream connection, attempts and USB reset.
  loader.sync=async()=>{assert.deepEqual(signals,expected,'ROM must receive the USB download-reset sequence before SYNC');return [1,new Uint8Array()];};
  loader.detectChip=async mode=>{await loader.openAndSync(mode,1);loader.chip={CHIP_NAME:'ESP32-S31',IMAGE_CHIP_ID:32,
    getChipDescription:async()=>'ESP32-S31',getChipFeatures:async()=>[],getCrystalFreq:async()=>40,readMac:async()=>'00:00:00:00:00:00'};};
  loader.runStub=async()=>{loader.IS_STUB=true;};loader.readFlashId=async()=>0x184020;
  return {loader,signals,registers};
}
const old=fixture();await assert.rejects(old.loader.main('no_reset'),/Failed to connect/);assert.deepEqual(old.signals,[]);
const current=fixture();await connectS31(current.loader);assert.deepEqual(current.signals,expected);assert(current.loader.IS_STUB);
assert.deepEqual(current.registers,[[0x20801018,0x50d83aa1],[0x20801000,0],[0x20801018,0],[0x20801020,0x50d83aa1],[0x2080101c,0x41234],[0x20801020,0]]);
await assert.rejects(connectS31({main:async()=>{},chip:{CHIP_NAME:'ESP32-S3',IMAGE_CHIP_ID:9},IS_STUB:true}),/S31/);
await assert.rejects(connectS31({main:async()=>{},chip:{CHIP_NAME:'ESP32-S31',IMAGE_CHIP_ID:32},IS_STUB:false}),/S31/);
let continueHandshake;const blocked={main:()=>new Promise(resolve=>continueHandshake=resolve)};
await assert.rejects(connectS31(blocked,10),/握手超时.*尚未写入/);continueHandshake();
assert.match(connectionMessage({name:'NetworkError'}),/关闭其他刷机页/);
assert.match(connectionMessage({name:'Error',message:'Failed to connect with the device'}),/官方固件/);
const installer=fs.readFileSync(new URL('../flash/metalio-installer.js',import.meta.url),'utf8');
assert.match(installer,/await connectS31\(loader\)/);assert(!installer.includes("loader.main('no_reset')"));
assert.match(installer,/首次安装必须从 Metalio 官方固件刷入/);
assert.match(fs.readFileSync(new URL('../flash/versions.js',import.meta.url),'utf8'),/首次安装必须从 Metalio 官方固件刷入/);
assert.match(fs.readFileSync(new URL('../flash/metalio/index.html',import.meta.url),'utf8'),/首次安装必须从 Metalio 官方固件刷入/);
console.log('PASS: actual Espressif USB reset precedes S31 SYNC; old no-reset fails; wrong chip/stub and hung handshakes stop; official-base instructions present');
