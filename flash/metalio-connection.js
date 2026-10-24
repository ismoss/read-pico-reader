/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：使用 Espressif USB JTAG 下载复位，限制只读握手等待；不执行 Flash 写入。
 * English: Use Espressif USB JTAG download reset and bound the read-only handshake; never write Flash.
 */
import {require} from './metalio-flash-core.js';

export async function connectS31(loader, timeoutMs=30000){
  let timer;
  try{
    await Promise.race([
      loader.main('usb_reset'),
      new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('S31 握手超时，尚未写入。请关闭此刷机页、重新进入 BOOT 下载模式，再连接。')),timeoutMs);})
    ]);
    require(loader.chip?.CHIP_NAME==='ESP32-S31'&&loader.chip.IMAGE_CHIP_ID===32&&loader.IS_STUB,'未连接到支持的 S31 下载口，尚未写入。');
  }finally{clearTimeout(timer);}
}

export function connectionMessage(error){
  if(error.name==='NotFoundError')return '已取消选择，尚未写入。';
  if(error.name==='NetworkError'||error.name==='InvalidStateError')return '串口无法打开或已断开，尚未写入。请关闭其他刷机页及串口监视器，重新进入 BOOT 后再连接。';
  if(/Failed to connect with the device/.test(error.message))return 'S31 下载握手失败，尚未写入。首次安装须从 Metalio 官方固件进入 BOOT；已有 kiikoread 请重新进入 BOOT 后再连接。';
  return error.message;
}
