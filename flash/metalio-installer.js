/* 中文：S31 专用网页安装，先验证再写入；与 Pico 安装器隔离。 / English: Verify-before-write S31 installation isolated from the Pico installer. */
import {require,equal,identity,partitionTable,selectedSlot} from './metalio-flash-core.js';
let active=false;
async function open(snapshot){
  if(active)return;active=true;
  const dialog=document.createElement('dialog');dialog.className='metalio-dialog';
  dialog.setAttribute('aria-labelledby','metalio-dialog-title');
  dialog.innerHTML=`<span class="card-number">METALIO / ESP32-S31</span><h2 id="metalio-dialog-title">安装 kiikoread</h2><p data-version></p><p>在墨水屏上进入「设置 → BOOT 刷机」，再选择下载串口。请关闭占用串口的其他刷机页面；USB 读卡模式不能刷机。</p><label><input type="checkbox" data-clear>清除机内设置（TF 卡文件保留）</label><label class="reinstall-option"><input type="checkbox" data-reinstall>重新安装基础包（用于原厂或其他固件）</label><p data-status role="status" aria-live="polite">尚未连接设备</p><progress max="100" value="0" aria-label="安装进度"></progress><details><summary>连接与校验记录</summary><pre data-log></pre></details><a data-backup hidden class="text-link">下载刷写前的应用备份</a><div class="dialog-actions"><button type="button" class="button primary" data-connect>连接并检查设备</button><button type="button" class="button primary" data-write hidden>确认刷入</button><button type="button" class="button secondary" data-close>关闭</button></div>`;
  document.body.appendChild(dialog);dialog.showModal();
  const q=s=>dialog.querySelector(s), status=q('[data-status]'), log=q('[data-log]'), progress=q('progress');
  const connect=q('[data-connect]'), write=q('[data-write]'), close=q('[data-close]');
  const clear=q('[data-clear]'), reinstall=q('[data-reinstall]');
  q('[data-version]').textContent=`Metalio EINK 4 Plus V1.5 / ${snapshot.manifest.version}。此包不适用于小纸 Pico。`;
  let loader,transport,bundle,plan,protectedBefore,backupURL,checking=false,writing=false,complete=false;
  const say=text=>{status.textContent=text;};
  const append=text=>{log.textContent=(log.textContent+String(text)+'\n').slice(-18000);log.scrollTop=log.scrollHeight;};
  const digest=data=>window.KiikoreadFlasher.digest(data);
  async function disconnect(){try{await transport?.disconnect();}catch{}transport=loader=null;}
  function lock(on){connect.disabled=close.disabled=clear.disabled=reinstall.disabled=on;write.disabled=on;}
  async function cleanup(){if(checking||writing)return;await disconnect();bundle?.dispose();if(backupURL)URL.revokeObjectURL(backupURL);dialog.remove();active=false;}
  close.addEventListener('click',cleanup);
  dialog.addEventListener('cancel',event=>{event.preventDefault();cleanup();});
  clear.addEventListener('change',()=>{if(plan){write.disabled=plan.full&&!clear.checked;write.textContent=plan.full?'确认重装并清除机内设置':clear.checked?'确认刷入并清除机内设置':'确认刷入，保留数据';}});
  reinstall.addEventListener('change',()=>{plan=null;write.hidden=true;say('安装方式已更改，请重新检查设备。');connect.hidden=false;});
  connect.addEventListener('click',async()=>{
    if(checking||writing)return;
    checking=true;lock(true);write.hidden=true;plan=null;
    try{
      // 串口选择由本次点击直接触发，下载等待不会丢失用户手势。 / Request the port directly from this click, before awaiting downloads.
      const port=await navigator.serial.requestPort({filters:[{usbVendorId:0x303a}]});
      await disconnect();say('正在下载并校验所选固件…');
      bundle?.dispose();bundle=await window.KiikoreadFlasher.prepare(snapshot.manifest,snapshot.manifestURL,snapshot.files,'metalio');
      const {ESPLoader,Transport}=await import('./vendor/metalio/esptool-js-0.7.0.js');
      transport=new Transport(port);loader=new ESPLoader({transport,baudrate:115200,romBaudrate:115200,terminal:{clean(){},write:append,writeLine:append}});
      say('正在握手并识别 S31…');await loader.main('no_reset');
      require(loader.chip?.CHIP_NAME==='ESP32-S31'&&loader.chip.IMAGE_CHIP_ID===32&&loader.IS_STUB,'未连接到支持的 S31 下载口，尚未写入。');
      const security=await loader.getSecurityInfo();
      require(security.chipId===32&&!security.parsedFlags.SECURE_BOOT_EN&&!security.parsedFlags.SECURE_DOWNLOAD_ENABLE&&((security.flashCryptCnt.toString(2).match(/1/g)||[]).length%2===0),'此设备已启用安全启动或 Flash 加密，不能使用此安装包。');
      const capacity=await loader.detectFlashSize();require(loader.flashSizeBytes(capacity)>=0x1000000,'Flash 容量不足 16 MB。');
      const mac=await loader.chip.readMac(loader);append(`S31 MAC: ${mac} / ${capacity}`);
      const table=bundle.parts.find(p=>p.name==='partitions.bin');partitionTable(table.data);
      say('正在核对分区与启动位置…');
      protectedBefore=await loader.readFlash(0x8000,0x8000);
      const expected=new Uint8Array(4096).fill(255);expected.set(table.data);
      const compatible=equal(protectedBefore.slice(0,4096),expected);
      if(reinstall.checked){
        require(clear.checked,'重新安装基础包需要勾选清除机内设置；TF 文件保留。');
        plan={full:true,mac};say(`已识别 S31 ${mac}。将重新安装基础包并清除机内设置。`);
      }else{
        require(compatible,'设备分区不同，尚未写入。如需从原厂切换，请勾选重新安装基础包及清除机内设置，再检查。');
        const slot=selectedSlot(protectedBefore.slice(0x5000,0x7000));
        const current=identity(await loader.readFlash(slot.address,4096));
        const app=bundle.parts.find(p=>p.name==='firmware.bin');require(Math.ceil(app.data.length/4096)*4096<=slot.size,'固件超过启动槽容量。');
        say('正在保留原应用，以便写入失败时恢复…');
        const before=await loader.readFlash(slot.address,slot.size);identity(before);
        if(backupURL)URL.revokeObjectURL(backupURL);backupURL=URL.createObjectURL(new Blob([before]));
        const backup=q('[data-backup]');backup.href=backupURL;backup.download=`Metalio-S31-before-${current}.bin`;backup.hidden=false;
        plan={full:false,slot,mac,current};say(`已识别 ${current}，保留分区和启动位置。确认后刷入 ${snapshot.manifest.version}。`);
      }
      connect.hidden=true;write.hidden=false;write.textContent=plan.full?'确认重装并清除机内设置':clear.checked?'确认刷入并清除机内设置':'确认刷入，保留数据';
    }catch(error){append(error.stack||error.message);say(error.name==='NotFoundError'?'已取消选择，尚未写入。':error.message);await disconnect();}
    finally{checking=false;lock(false);if(plan)write.disabled=plan.full&&!clear.checked;}
  });
  write.addEventListener('click',async()=>{
    if(!plan||!loader||writing||complete)return;
    writing=true;lock(true);let imageVerified=false;
    try{
      // 再次核对检查后设备未变；写入中不更改已绑定的版本。 / Recheck the device and retain the clicked release throughout writing.
      require(await loader.chip.readMac(loader)===plan.mac,'设备身份发生变化。');
      require(equal(await loader.readFlash(0x8000,0x8000),protectedBefore),'检查后启动记录发生变化，请重新连接。');
      const app=bundle.parts.find(p=>p.name==='firmware.bin');
      const parts=plan.full?bundle.parts.map(p=>({data:p.data,address:p.offset})):[{data:app.data,address:plan.slot.address}];
      parts.sort((a,b)=>a.address-b.address);
      say('正在写入，请保持页面开启和 USB 连接…');append(`Write ${snapshot.manifest.version}: ${plan.full?'full install':`0x${plan.slot.address.toString(16)}`}`);
      await loader.writeFlash({fileArray:parts,flashSize:'keep',flashMode:'keep',flashFreq:'keep',eraseAll:false,compress:true,reportProgress:(index,written,total)=>{progress.value=Math.floor((index+written/total)/parts.length*75);}});
      for(let i=0;i<parts.length;i++){
        say(`正在读回校验 ${i+1}/${parts.length}…`);
        const actual=await loader.readFlash(parts[i].address,parts[i].data.length);
        require(await digest(actual)===await digest(parts[i].data),'设备读回校验失败，不能确认安装成功。');progress.value=75+Math.floor((i+1)/parts.length*20);
      }
      imageVerified=true;
      if(clear.checked){
        say('固件已校验，正在清除机内设置…');
        await loader.writeFlash({fileArray:[{address:0x9000,data:new Uint8Array(0x4000).fill(255)}],flashSize:'keep',flashMode:'keep',flashFreq:'keep',eraseAll:false,compress:true});
      }
      const after=await loader.readFlash(0x8000,0x8000);
      if(!plan.full){
        require(equal(after.slice(0,4096),protectedBefore.slice(0,4096))&&equal(after.slice(0x5000),protectedBefore.slice(0x5000)),'保护区校验失败，请保留记录。');
        require(clear.checked?after.slice(0x1000,0x5000).every(v=>v===255):equal(after.slice(0x1000,0x5000),protectedBefore.slice(0x1000,0x5000)),'机内设置校验失败。');
      }else require(after.slice(0x1000,0x5000).every(v=>v===255),'清除设置校验失败。');
      complete=true;progress.value=100;say('固件与数据保护校验通过，正在重启…');
      // 按 Espressif S31 继承的 watchdog_reset 顺序复位，不写 eFuse。 / Follow Espressif S31's inherited watchdog_reset sequence; never write eFuses.
      try{await loader.writeReg(0x20801018,0x50d83aa1);await loader.writeReg(0x20801004,2000);await loader.writeReg(0x20801000,0xd0000102);await loader.writeReg(0x20801018,0);}catch(error){append(`Reset: ${error.message}`);}
      say(`安装及读回校验完成。请查看设备是否已进入 ${snapshot.manifest.version}；若停在 BOOT，请短按复位。`);write.hidden=true;connect.hidden=true;
    }catch(error){append(error.stack||error.message);say(`${imageVerified?'固件已校验，但收尾未完成':'操作未完成，不能确认写入成功'}：${error.message}。请保留记录和原应用备份。`);write.hidden=true;connect.hidden=false;plan=null;}
    finally{writing=false;lock(false);await disconnect();}
  });
}
window.MetalioInstaller=Object.freeze({open});window.dispatchEvent(new Event('metalio-installer-ready'));
