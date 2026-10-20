import createPico from './pico.js';
let core, ready=false, busy=false, saved, dirty=false;
const version='0.3.3-rc97';
function frame(id){
  const start=core._sim_frame();
  const packed=core.HEAPU8.slice(start,start+1216*684/2);
  postMessage({type:'frame',id,targets:{settings:Array.from({length:11},(_,i)=>core._sim_settings_target(i)),reader:Array.from({length:6},(_,i)=>{const v=core._sim_reader_target(i);return [v>>>16,v&65535];})},pixels:packed.buffer,busy:!!core._sim_book_busy(),page:core.UTF8ToString(core._sim_page()),view:{settings:core._sim_settings_view(),book:core._sim_book_view(),panel:core._sim_book_panel(),home:core._sim_home_view()},locked:!!core._sim_locked(),quick:!!core._sim_quick_open(),version},[packed.buffer]);
}
function save(){if(demonstration)return;
  clearTimeout(saved);saved=setTimeout(()=>core.FS.syncfs(false,error=>{if(error)postMessage({type:'notice',text:'浏览器未允许保存，下次打开将重新体验。'});}),400);
}
let demonstration=false;
async function initialize(options={}){demonstration=!!options.demo;
  core=await createPico({locateFile:name=>new URL(name,import.meta.url).href,print:()=>{},printErr:message=>console.warn(message)});
  core.FS.mkdirTree('/Data/nvs');core.FS.mkdirTree('/Data/flash');core.FS.mkdirTree('/Data/card/fonts');core.FS.mkdirTree('/Data/card/pictures');
  try{
    if(demonstration)throw new Error('demo');core.FS.mount(core.IDBFS,{},'/Data/nvs');
    await new Promise((resolve,reject)=>core.FS.syncfs(true,error=>error?reject(error):resolve()));
  }catch(error){if(!demonstration)postMessage({type:'notice',text:'本次设置在当前浏览器会话内保留。'});}
  core._sim_assets_init();core._sim_seed();core._sim_init();frame();ready=true;postMessage({type:'ready',version});
}
onmessage=async ({data})=>{
  try{
    if(data.type==='init'){await initialize(data);return;}
    if(!ready)return;
    if(data.type==='command'){
      if(typeof data.command!=='string'||data.command.length>1024)return;
      core.ccall('sim_command',null,['string'],[data.command]);frame(data.id);
      if(data.command!=='tick'&&!/^(down|move) /.test(data.command))save();
    }else if(data.type==='file'){
      const name=data.name.replace(/[\/\\\x00-\x1f]/g,'_');
      if(!name||data.bytes.byteLength>20*1024*1024)throw new Error('文件过大，请选择 20 MB 以内的文件。');
      const font=/\.(ttf|otf)$/i.test(name),book=/\.(txt|epub)$/i.test(name);
      if(!font&&!book)throw new Error('支持 EPUB、TXT、TTF 和 OTF 文件。');
      const directory=font?'fonts':'books';
      if(core._sim_locked())throw new Error('请先解锁模拟器。');
      let unique=name,suffix=1;
      while(core.FS.analyzePath(`/Data/card/${directory}/${unique}`).exists)unique=name.replace(/(\.[^.]+)$/,` (${suffix++})$1`);
      core.FS.writeFile(`/Data/card/${directory}/${unique}`,new Uint8Array(data.bytes));
      core.ccall('sim_command',null,['string'],[font?`font /sdcard/fonts/${unique}`:'rescan']);frame();save();
      postMessage({type:'notice',text:`已导入 ${unique}，文件仅在本次浏览器会话内使用。`});
    }else if(data.type==='reset'){
      core._sim_shutdown();
      function remove(path){for(const item of core.FS.readdir(path)){if(item==='.'||item==='..')continue;const p=`${path}/${item}`;if(core.FS.isDir(core.FS.stat(p).mode)){remove(p);core.FS.rmdir(p);}else core.FS.unlink(p);}}
      remove('/Data/nvs');await new Promise(resolve=>core.FS.syncfs(false,()=>resolve()));postMessage({type:'reset'});
    }
  }catch(error){postMessage({type:ready?'notice':'error',text:error.message||'模拟器启动失败，请重试。'});}
};
