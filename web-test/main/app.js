const el=id=>document.getElementById(id);
let state=null,busy=false,online=false;
function enable(){document.querySelectorAll('[data-action],#power-toggle').forEach(b=>b.disabled=busy||!online);}
function render(s){if(s.simulation!==true)throw Error('设备模式不匹配');state=s;online=true;el('dot').classList.add('online');el('connection').textContent='设备已连接 · 本地模拟';el('power').textContent=s.power?'已开启':'已关闭';el('power-toggle').setAttribute('aria-checked',String(s.power));el('brightness').textContent=s.brightness;el('temperature').textContent=s.temperature;el('brightness-track').style.width=s.brightness+'%';el('temperature-track').style.left=((s.temperature-2800)/2200*100)+'%';window.dispatchEvent(new CustomEvent('lamp-state',{detail:s}));enable();}
async function request(path,options){const ctrl=new AbortController(),timer=setTimeout(()=>ctrl.abort(),6000);try{const r=await fetch(path,{...options,signal:ctrl.signal,cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);return await r.json();}finally{clearTimeout(timer);}}
async function refresh(){if(busy)return;busy=true;try{render(await request('/api/state'));el('message').textContent='模拟控制 · 已接收 '+state.actions+' 次操作';}catch(e){online=false;el('dot').classList.remove('online');el('connection').textContent='连接中断';el('message').textContent='请检查设备供电、Wi-Fi 或访问地址';}finally{busy=false;enable();}}
async function action(name){if(busy||!online)return;busy=true;enable();try{render(await request('/api/action',{method:'POST',headers:{'Content-Type':'text/plain'},body:name}));el('message').textContent='ESP32 已确认 · 模拟操作 '+state.actions+' 次';}catch(e){online=false;el('dot').classList.remove('online');el('message').textContent='操作未确认，重新连接后查看状态';}finally{busy=false;enable();}}
document.querySelectorAll('[data-action]').forEach(b=>b.addEventListener('click',()=>action(b.dataset.action)));
el('power-toggle').addEventListener('click',()=>action(state.power?'off':'on'));
refresh();setInterval(refresh,3000);
// Load graphics independently: unsupported WebGL must not disable controls.
import('/scene.js?v=3d1').then(m=>{m.startScene(el('scene'));if(state)window.dispatchEvent(new CustomEvent('lamp-state',{detail:state}));}).catch(e=>{console.error(e);el('render-error').textContent='3D 预览暂不可用，请使用支持 WebGL2/WebGL 的浏览器。下方控件仍可使用。';});
