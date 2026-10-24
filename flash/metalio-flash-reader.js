/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：按 Espressif READ_FLASH 协议限制在途包，确认每包并收齐 MD5 结束帧；不修改 Flash。
 * English: Bound in-flight Espressif READ_FLASH frames, acknowledge each and validate the final MD5 frame; never modify Flash.
 */
import {require} from './metalio-flash-core.js';
import SparkMD5 from './vendor/metalio/spark-md5-3.0.2.js';
const failed=new WeakSet();
const word=value=>{const data=new Uint8Array(4);new DataView(data.buffer).setUint32(0,value,true);return data;};
const hex=data=>Array.from(data,b=>b.toString(16).padStart(2,'0')).join('');

export async function readS31Flash(loader,address,size,onProgress=()=>{},timeoutMs=Math.max(15000,Math.ceil(size/65536)*1000)){
  require(loader.chip?.IMAGE_CHIP_ID===32&&loader.IS_STUB,'需要已识别的 S31 下载程序。');
  require(!failed.has(loader),'读取会话已中断，请关闭页面并重新连接。');
  require(Number.isSafeInteger(address)&&Number.isSafeInteger(size)&&address>=0&&size>0&&address+size<=0x1000000,'读取范围超出 S31 Flash。');
  const data=new Uint8Array(size),hash=new SparkMD5.ArrayBuffer();
  const packetSize=1024,windowSize=4;
  const request=new Uint8Array(16),view=new DataView(request.buffer);
  [address,size,packetSize,windowSize].forEach((value,index)=>view.setUint32(index*4,value,true));
  let timer;
  try{
    await Promise.race([
      (async()=>{
        require(await loader.checkCommand('read flash',loader.ESP_READ_FLASH,request,undefined,undefined,5000)===0,'S31 拒绝读取 Flash。');
        let received=0;
        while(received<size){
          const packet=await loader.transport.read(5000);
          require(packet instanceof Uint8Array&&packet.length===Math.min(packetSize,size-received),'Flash 数据包长度异常，请重新连接。');
          data.set(packet,received);hash.append(packet.slice().buffer);received+=packet.length;
          await loader.transport.write(word(received));onProgress(received,size);
        }
        // 必须等待结束帧，才允许下一条命令使用同一串口。
        // Consume the final frame before issuing another command on this transport.
        const digest=await loader.transport.read(5000);
        require(digest instanceof Uint8Array&&digest.length===16,'Flash 读取缺少结束校验包。');
        require(hash.end()===hex(digest),'Flash 读回数据 MD5 不匹配，已停止。');
      })(),
      new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('Flash 读取超时，已停止；请关闭此页面，重新进入 BOOT 后连接。')),timeoutMs);})
    ]);
    return data;
  }catch(error){failed.add(loader);throw error;}finally{clearTimeout(timer);hash.destroy();}
}
