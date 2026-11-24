import * as THREE from '/three.module.min.js?v=1601';
export function startScene(canvas){
const renderer=new THREE.WebGLRenderer({canvas,antialias:true,alpha:false,powerPreference:'low-power'});
renderer.setPixelRatio(Math.min(devicePixelRatio,1.6));renderer.shadowMap.enabled=true;renderer.shadowMap.type=THREE.PCFSoftShadowMap;renderer.outputColorSpace=THREE.SRGBColorSpace;renderer.toneMapping=THREE.ACESFilmicToneMapping;renderer.toneMappingExposure=1.15;
const scene=new THREE.Scene();scene.background=new THREE.Color('#dcd8cf');scene.fog=new THREE.Fog('#dcd8cf',4,8);
const camera=new THREE.PerspectiveCamera(37,1,.02,20),target=new THREE.Vector3(0,.40,0);
let yaw=.30,pitch=.22,distance=2.45,dirty=true,visible=true;
function view(){camera.position.set(target.x+Math.sin(yaw)*Math.cos(pitch)*distance,target.y+Math.sin(pitch)*distance,target.z+Math.cos(yaw)*Math.cos(pitch)*distance);camera.lookAt(target);dirty=true;}
const white=new THREE.MeshStandardMaterial({color:'#f3f3ed',roughness:.36,metalness:.05});const dark=new THREE.MeshStandardMaterial({color:'#535954',roughness:.48});const metal=new THREE.MeshStandardMaterial({color:'#8d9691',metalness:.7,roughness:.28});
function box(w,h,d,mat,x,y,z){const o=new THREE.Mesh(new THREE.BoxGeometry(w,h,d),mat);o.position.set(x,y,z);o.castShadow=true;o.receiveShadow=true;scene.add(o);return o;}
function cylinder(r,h,mat,x,y,z){const o=new THREE.Mesh(new THREE.CylinderGeometry(r,r,h,24),mat);o.position.set(x,y,z);o.castShadow=true;scene.add(o);return o;}
// World units are meters: 80cm light bar, ~40cm rigid stem plus flexible neck.
const desk=box(1.65,.045,.93,new THREE.MeshStandardMaterial({color:'#161a19',roughness:.64}),0,-.025,.17);
const wallMaterial=new THREE.MeshStandardMaterial({color:'#eee6d6',roughness:.95});
box(10,5,.04,wallMaterial,0,.90,-.43);
// Rear-edge clamp: no desk-base plate. Lower jaw and threaded screw are visible from side.
box(.075,.016,.105,white,0,.011,-.235);box(.065,.085,.018,white,0,-.025,-.29);box(.065,.015,.095,white,0,-.073,-.247);
cylinder(.006,.065,metal,0,-.055,-.225);cylinder(.017,.008,dark,0,-.021,-.225);cylinder(.023,.012,white,0,-.092,-.225);
box(.032,.405,.034,white,0,.22,-.235);box(.036,.018,.038,white,0,.415,-.235);
const curve=new THREE.CatmullRomCurve3([new THREE.Vector3(0,.42,-.235),new THREE.Vector3(0,.58,-.23),new THREE.Vector3(0,.705,-.19),new THREE.Vector3(0,.745,-.07),new THREE.Vector3(0,.745,.015)]);
const neck=new THREE.Mesh(new THREE.TubeGeometry(curve,40,.016,12,false),white);neck.castShadow=true;scene.add(neck);
const head=box(.80,.018,.056,white,0,.754,.018);
box(.81,.007,.058,white,0,.76,.018);
const diffuserMat=new THREE.MeshStandardMaterial({color:'#eeeade',roughness:.5,emissive:'#ffdfa3',emissiveIntensity:0});
box(.773,.003,.043,diffuserMat,0,.743,.022);
// Dark central control strip, visible on the front edge of the bar.
box(.15,.016,.0025,dark,0,.756,.0475);
for(let i=0;i<6;i++){const button=new THREE.Mesh(new THREE.CircleGeometry(.0023,12),new THREE.MeshBasicMaterial({color:'#d8ddd0'}));button.position.set((i-2.5)*.023,.756,.049);scene.add(button);}
const ambient=new THREE.HemisphereLight('#f4f1e6','#898a81',1.65);scene.add(ambient);const key=new THREE.DirectionalLight('#fff8e9',2.2);key.position.set(-1.5,2.5,2);key.castShadow=true;key.shadow.mapSize.set(1024,1024);key.shadow.camera.left=-1.2;key.shadow.camera.right=1.2;key.shadow.camera.top=1.3;key.shadow.camera.bottom=-.5;key.shadow.normalBias=.008;key.shadow.radius=4;scene.add(key);
// A row of soft downlights approximates the long diffuse source (no giant cone).
function theme(mode){const night=mode==='dark';const bg=night?'#202923':'#dcd8cf';scene.background.set(bg);scene.fog.color.set(bg);wallMaterial.color.set(night?'#3b453c':'#eee6d6');ambient.intensity=night?.65:1.65;key.intensity=night?.85:2.2;dirty=true;}
window.addEventListener('theme-change',e=>theme(e.detail));theme(document.documentElement.dataset.theme);
const lights=[];for(const x of [-.29,-.145,0,.145,.29]){const light=new THREE.SpotLight('#ffe0aa',0,2.5,Math.PI/3,.95,1.5);light.position.set(x,.735,.022);light.target.position.set(x,.002,.13);scene.add(light,light.target);lights.push(light);}
// Transparent gradient pools make subtle illumination readable on a black desktop.
const texCanvas=document.createElement('canvas');texCanvas.width=128;texCanvas.height=128;const ctx=texCanvas.getContext('2d');const gradient=ctx.createRadialGradient(64,64,0,64,64,64);gradient.addColorStop(0,'rgba(255,255,255,.65)');gradient.addColorStop(.4,'rgba(255,255,255,.30)');gradient.addColorStop(1,'rgba(255,255,255,0)');ctx.fillStyle=gradient;ctx.fillRect(0,0,128,128);const texture=new THREE.CanvasTexture(texCanvas);
const poolMat=new THREE.MeshBasicMaterial({map:texture,color:'#ffe1a8',transparent:true,opacity:0,depthWrite:false,blending:THREE.AdditiveBlending});const pool=new THREE.Mesh(new THREE.PlaneGeometry(1.15,.78),poolMat);pool.rotation.x=-Math.PI/2;pool.position.set(0,-.001,.16);scene.add(pool);
const wallMat=poolMat.clone();const wallPool=new THREE.Mesh(new THREE.PlaneGeometry(1.3,.75),wallMat);wallPool.position.set(0,.39,-.407);scene.add(wallPool);
let desired=0,current=0,desiredColor=new THREE.Color('#fff1d9'),currentColor=desiredColor.clone();
function kelvin(k){const warm=new THREE.Color('#ffcb83'),cool=new THREE.Color('#e6f0ff');return warm.lerp(cool,THREE.MathUtils.clamp((k-2800)/2200,0,1));}
window.addEventListener('lamp-state',e=>{desired=e.detail.power_known&&e.detail.power?e.detail.brightness/100:0;desiredColor=kelvin(e.detail.temperature);dirty=true;});
const pointers=new Map();let pinch=0;
canvas.addEventListener('pointerdown',e=>{pointers.set(e.pointerId,{x:e.clientX,y:e.clientY});canvas.setPointerCapture(e.pointerId);pinch=0;});
canvas.addEventListener('pointermove',e=>{const p=pointers.get(e.pointerId);if(!p)return;if(pointers.size===1){yaw-=(e.clientX-p.x)*.005;pitch=THREE.MathUtils.clamp(pitch+(e.clientY-p.y)*.003,.02,.85);}pointers.set(e.pointerId,{x:e.clientX,y:e.clientY});if(pointers.size===2){const a=[...pointers.values()];const gap=Math.hypot(a[0].x-a[1].x,a[0].y-a[1].y);if(pinch)distance=THREE.MathUtils.clamp(distance*pinch/gap,1.35,3.8);pinch=gap;}view();});
for(const event of ['pointerup','pointercancel','lostpointercapture'])canvas.addEventListener(event,e=>{pointers.delete(e.pointerId);pinch=0;});
canvas.addEventListener('wheel',e=>{e.preventDefault();distance=THREE.MathUtils.clamp(distance*Math.exp(e.deltaY*.001),1.35,3.8);view();},{passive:false});
document.getElementById('reset-view').addEventListener('click',()=>{yaw=.30;pitch=.22;distance=canvas.clientWidth<650?2.9:2.45;view();});
function resize(){const w=canvas.clientWidth,h=canvas.clientHeight;if(!w||!h)return;renderer.setSize(w,h,false);camera.aspect=w/h;camera.updateProjectionMatrix();distance=w<650?2.9:2.45;view();}new ResizeObserver(resize).observe(canvas);resize();
document.addEventListener('visibilitychange',()=>{visible=!document.hidden;dirty=true;});canvas.addEventListener('webglcontextlost',e=>{e.preventDefault();visible=false;document.getElementById('render-error').textContent='3D 绘图上下文中断，请刷新页面；控制按钮仍可使用。';});
let last=0;function frame(now){requestAnimationFrame(frame);if(!visible||now-last<33)return;last=now;const moving=Math.abs(current-desired)>.001||Math.abs(currentColor.r-desiredColor.r)+Math.abs(currentColor.g-desiredColor.g)+Math.abs(currentColor.b-desiredColor.b)>.001;if(moving){current=THREE.MathUtils.lerp(current,desired,.16);currentColor.lerp(desiredColor,.16);diffuserMat.emissive.copy(currentColor);diffuserMat.emissiveIntensity=current*2.5;poolMat.color.copy(currentColor);poolMat.opacity=current*.18;wallMat.color.copy(currentColor);wallMat.opacity=current*.20;lights.forEach(light=>{light.color.copy(currentColor);light.intensity=current*4.2;});dirty=true;}if(dirty){renderer.render(scene,camera);dirty=false;}}requestAnimationFrame(frame);
}
