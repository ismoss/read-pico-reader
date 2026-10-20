import {installTours} from './tour-controller.js';
const embedded=new URLSearchParams(location.search).has('embed');document.body.className=embedded?'embedded':'standalone';
const canvas=document.getElementById('screen'),ctx=canvas.getContext('2d',{alpha:false}),image=ctx.createImageData(684,1216),loading=document.getElementById('loading'),notice=document.getElementById('notice');
let worker=new Worker(new URL('./worker.js',import.meta.url),{type:'module'}),personalWorker=worker;let ready=false,pressed=false,pendingMove=null,raf=0,noticeTimer,lastState,requestId=0;const requests=new Map(),readyCallbacks=[];let bootResolve,bootReject;
function command(value){if(ready)worker.postMessage({type:'command',command:value});}
function announce(text){notice.textContent=text;clearTimeout(noticeTimer);noticeTimer=setTimeout(()=>notice.textContent='',8000);}
function receive(event){if(event.currentTarget!==worker)return;const {data}=event;
  if(data.type==='frame'){lastState=data;
    const bytes=new Uint8Array(data.pixels),rgba=image.data;
    for(let y=0;y<1216;y++)for(let x=0;x<684;x++){const px=1215-y,b=bytes[x*608+(px>>1)],v=((px&1)?b>>4:b&15)*17,i=(y*684+x)*4;rgba[i]=rgba[i+1]=rgba[i+2]=v;rgba[i+3]=255;}
    ctx.putImageData(image,0,0);canvas.dataset.view=JSON.stringify(data.view);canvas.dataset.quick=String(data.quick);canvas.dataset.page=data.page;canvas.dataset.locked=String(data.locked);document.getElementById('state').textContent=data.locked?'已锁屏':data.page.split(' ')[0];
    document.querySelectorAll('.tabs button').forEach((button,i)=>{const labels=['首页','图书','文件管理','设置'];if(data.page.startsWith(labels[i])&&!data.locked)button.setAttribute('aria-current','page');else button.removeAttribute('aria-current');});
  if(data.id&&requests.has(data.id)){requests.get(data.id).resolve(data);requests.delete(data.id);}
  }else if(data.type==='ready'){ready=true;loading.hidden=true;document.getElementById('version').textContent=data.version;document.querySelectorAll('button').forEach(b=>b.disabled=false);bootResolve?.();bootResolve=null;readyCallbacks.splice(0).forEach(fn=>fn());}
  else if(data.type==='notice')announce(data.text);
  else if(data.type==='error'){loading.classList.add('error');loading.querySelector('strong').textContent='暂时没有启动成功';document.getElementById('loading-detail').textContent=data.text;document.getElementById('retry').hidden=false;bootReject?.(new Error(data.text));}
  else if(data.type==='reset')location.reload();
}
function failed(){loading.hidden=false;loading.classList.add('error');loading.querySelector('strong').textContent='模拟器未能加载';document.getElementById('loading-detail').textContent='请重新加载后再试。';document.getElementById('retry').hidden=false;bootReject?.(new Error('固件实例加载失败'));}
function attach(w){w.addEventListener('message',receive);w.addEventListener('error',failed);}
attach(worker);
document.querySelectorAll('[data-command]').forEach(button=>button.addEventListener('click',()=>command(button.dataset.command)));
function point(event){const b=canvas.getBoundingClientRect();return [Math.max(0,Math.min(683,Math.round((event.clientX-b.left)*684/b.width))),Math.max(0,Math.min(1215,Math.round((event.clientY-b.top)*1216/b.height)))];}
canvas.addEventListener('pointerdown',event=>{if(!ready)return;pressed=true;canvas.focus({preventScroll:true});canvas.setPointerCapture(event.pointerId);command('down '+point(event).join(' '));});
canvas.addEventListener('pointermove',event=>{if(!pressed)return;pendingMove=point(event);if(!raf)raf=requestAnimationFrame(()=>{raf=0;if(pendingMove){command('move '+pendingMove.join(' '));pendingMove=null;}});});
function release(event){if(!pressed)return;pressed=false;if(raf){cancelAnimationFrame(raf);raf=0;}if(pendingMove){command('move '+pendingMove.join(' '));pendingMove=null;}command('up '+point(event).join(' '));}
canvas.addEventListener('pointerup',release);canvas.addEventListener('pointercancel',release);
canvas.addEventListener('keydown',event=>{const commands={ArrowLeft:'key 0',ArrowRight:'key 2',' ':'key 1',Escape:'page 0'};if(commands[event.key]){event.preventDefault();command(commands[event.key]);}});
document.getElementById('retry').onclick=()=>location.reload();
document.getElementById('reset').onclick=()=>{if(ready&&confirm('重置这台浏览器里的体验设置、阅读进度和密码？'))worker.postMessage({type:'reset'});};
document.getElementById('import').onchange=async event=>{for(const file of event.target.files){if(file.size>20*1024*1024){announce('请选择 20 MB 以内的文件。');continue;}const bytes=await file.arrayBuffer();worker.postMessage({type:'file',name:file.name,bytes},[bytes]);}event.target.value='';};
document.getElementById('export').onclick=()=>{if(!ready)return;canvas.toBlob(blob=>{if(!blob)return;const url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download='kiikoread-rc97-684x1216.png';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);});};
setInterval(()=>{if(ready&&!pressed&&!document.hidden)command('tick');},250);
window.addEventListener('pagehide',()=>{if(worker===personalWorker)command('page 0');});
document.querySelectorAll('button').forEach(b=>{if(b.id!=='retry')b.disabled=true;});worker.postMessage({type:'init'});

function request(value){if(!ready)return Promise.reject(new Error('模拟器尚未就绪'));const id=++requestId;return new Promise((resolve,reject)=>{const timer=setTimeout(()=>{requests.delete(id);reject(new Error('操作未完成，请重试演示'));},10000);requests.set(id,{resolve:data=>{clearTimeout(timer);resolve(data);}});worker.postMessage({type:'command',command:value,id});});}
const guide=installTours({command:value=>worker!==personalWorker?request(value):Promise.reject(new Error("演示已结束")),state:()=>lastState,notice:announce,onReady:fn=>ready?fn():readyCallbacks.push(fn),begin:async isCurrent=>{if(worker===personalWorker&&ready)await request("suspend");if(!isCurrent())return;bootResolve?.();bootResolve=bootReject=null;if(worker!==personalWorker)worker.terminate();ready=false;worker=new Worker(new URL('./worker.js',import.meta.url),{type:'module'});attach(worker);return new Promise((resolve,reject)=>{bootResolve=resolve;bootReject=reject;worker.postMessage({type:'init',demo:true});});},end:()=>{bootResolve?.();bootResolve=bootReject=null;if(worker!==personalWorker)worker.terminate();worker=personalWorker;ready=true;loading.hidden=true;command('refresh');}});
document.getElementById('ask-guide').onclick=()=>{if(embedded){parent.postMessage({type:'kiikoread:ask-manual'},location.origin);}else{document.querySelector('.manual-launch')?.click();}};
if(!embedded){const style=document.createElement('link');style.rel='stylesheet';style.href='../assets/manual-helper.css';document.head.append(style);const search=document.createElement('script');search.src='../assets/manual-search.js';search.onload=()=>{const helper=document.createElement('script');helper.src='../assets/manual-helper.js';helper.dataset.index='../manual/manual-index.json';helper.dataset.manual='../manual/';document.head.append(helper);};document.head.append(search);}

document.addEventListener('kiikoread:start-guide',event=>{if(typeof event.detail?.id==='string')guide.start(event.detail.id);});
if(embedded){let heightRaf;new ResizeObserver(()=>{cancelAnimationFrame(heightRaf);heightRaf=requestAnimationFrame(()=>parent.postMessage({type:'kiikoread:simulator-height',height:Math.ceil(document.querySelector('.experience').getBoundingClientRect().height)+2},location.origin));}).observe(document.querySelector('.experience'));}
