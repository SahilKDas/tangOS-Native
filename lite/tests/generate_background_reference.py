"""Extract original mover functions; generate deterministic cross-language reference fixtures."""
import argparse, pathlib, re, subprocess
root=pathlib.Path(__file__).resolve().parents[2]
source=(root/'console/src/renderer/src/components/GradientBackground.tsx').read_text(encoding='utf-8')
functions=source[source.index('function makeMover()'):source.index('// gently push apart')]
functions=re.sub(r':\s*Record<[^>]+>', '', functions)
functions=functions.replace(': Mover[]', '').replace(': Mover', '').replace(': MotionMode', '').replace(': number', '').replace(': string', '').replace(': void', '').replace(': { x; y }','')
functions=functions.replace(': { x: number; y: number }','')
functions=re.sub(r':\s*\{ x; y \}', '', functions)
code=r"""
const TAU=Math.PI*2,FLOW=10,MARGIN=10;
let seed=0;
Math.random=()=>{seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed/4294967296};
const rand=(a,b)=>a+Math.random()*(b-a);
const clampPct=(v,m)=>Math.max(m,Math.min(100-m,v));
const wrapPct=v=>(((v+20)%140)+140)%140-20;
"""+functions+r"""
const homes=[
[[76,26],[24,74],[58,84],[32,42]],
[[71.5,38.7],[67.9,70.5],[32.5,53.5],[38.5,84.9]],
[[60.9,34.1],[37.1,76.1],[68.8,72.4],[87,48.5]],
[[71.5,38.7],[67.9,70.5],[32.5,53.5],[38.5,84.9]],
[[87,52.8],[13.5,47],[29.1,80.3]]];
for(let theme=0;theme<5;theme++){
 seed=0x54a69000+theme;
 const stops=homes[theme].map(([x,y])=>makeStopAt('',x,y));spaceHomes(stops,32);
 const bubbles=[];
 for(let i=0;i<3;i++){const size=rand(90,190);const o=Object.assign(makeMover(),{rot:rand(-8,8),size});bubbles.push(o);}
 spaceHomes(bubbles,24);
 for(const [group,bubble] of [[stops,0],[bubbles,1]])
  group.forEach((o,index)=>{
   for(const seconds of [0,.01,.1,1,7,60,400,4000]){
    const m={...o},speed=bubble?.41:theme===4?.22:.35,t=seconds*speed;
    const p=seconds===0?{x:m.rx,y:m.ry}:motion(m,bubble?'rising':'leftright',t,t);
    const rotation=(m.rot??0)*Math.sin(t*m.fx*.5+m.ph);
    console.log([theme,bubble,index,seconds,p.x,p.y,rotation,m.size??0].join(','));
   }
  });
}
"""
run=subprocess.run(['node','-e',code],capture_output=True,text=True)
if run.returncode: raise RuntimeError(run.stderr)
destination=root/'lite/renderer/tests/background-reference.csv'
destination.parent.mkdir(parents=True,exist_ok=True)
parser=argparse.ArgumentParser()
parser.add_argument('--check',action='store_true')
args=parser.parse_args()
if args.check:
    if destination.read_text(encoding='utf-8') != run.stdout:
        raise RuntimeError('Original background fixtures changed; regenerate and review the native translation')
else:
    destination.write_text(run.stdout,encoding='utf-8')
print('Generated',len(run.stdout.splitlines()),'original background motion reference cases')