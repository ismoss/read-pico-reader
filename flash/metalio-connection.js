/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：使用 Espressif USB JTAG 下载复位，限制只读握手等待；不执行 Flash 写入。
 * English: Use Espressif USB JTAG download reset and bound the read-only handshake; never write Flash.
 */
import {require} from './metalio-flash-core.js';

export async function prepareS31Download(loader){
  require(loader.chip?.CHIP_NAME==='ESP32-S31'&&loader.chip.IMAGE_CHIP_ID===32,'不是 S31 下载口，尚未写入。');
  if(loader.secureDownloadMode||loader.syncStubDetected)return;
  require(loader.transport.getVid()===0x303a&&loader.transport.getPid()===0x1001,'需要 S31 USB JTAG 下载口。');
  // S31 寄存器与处理顺序取自 Espressif esptool 5.5.0；运行期看门狗仍由固件启动代码配置。
  // Follow Espressif esptool 5.5.0's S31 register map/sequence; firmware startup still configures runtime watchdogs.
  await loader.writeReg(0x20801018,0x50d83aa1);
  await loader.writeReg(0x20801000,0);
  await loader.writeReg(0x20801018,0);
  await loader.writeReg(0x20801020,0x50d83aa1);
  const swd=await loader.readReg(0x2080101c);
  await loader.writeReg(0x2080101c,(swd|0x40000)>>>0);
  await loader.writeReg(0x20801020,0);
}

export async function connectS31(loader, timeoutMs=30000){
  let timer;const runStub=loader.runStub;
  loader.runStub=async function(){await prepareS31Download(this);return runStub.call(this);};
  try{
    await Promise.race([
      loader.main('usb_reset'),
      new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('S31 握手超时，尚未写入。请关闭此刷机页、重新进入 BOOT 下载模式，再连接。')),timeoutMs);})
    ]);
    require(loader.chip?.CHIP_NAME==='ESP32-S31'&&loader.chip.IMAGE_CHIP_ID===32&&loader.IS_STUB,'未连接到支持的 S31 下载口，尚未写入。');
  }finally{clearTimeout(timer);loader.runStub=runStub;}
}

export function connectionMessage(error){
  if(error.name==='NotFoundError')return '已取消选择，尚未写入。';
  if(error.name==='NetworkError'||error.name==='InvalidStateError')return '串口无法打开或已断开，尚未写入。请关闭其他刷机页及串口监视器，重新进入 BOOT 后再连接。';
  if(/Failed to connect with the device/.test(error.message))return 'S31 下载握手失败，尚未写入。首次安装须从 Metalio 官方固件进入 BOOT；已有 kiikoread 请重新进入 BOOT 后再连接。';
  return error.message;
}
