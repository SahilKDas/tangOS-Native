"""Compare native shadow pixels with the original CSS in installed Edge, not shipped."""
import argparse, json, pathlib, re, struct, subprocess

p = argparse.ArgumentParser()
p.add_argument('--probe', required=True)
p.add_argument('--output', required=True)
p.add_argument('--dependencies', default='lite/out/reference-tools')
p.add_argument('--browser', default=r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe')
a = p.parse_args()
root = pathlib.Path(__file__).resolve().parents[2]
out = pathlib.Path(a.output).resolve()
out.mkdir(parents=True, exist_ok=True)
css = (root/'console/src/renderer/src/app.css').read_text(encoding='utf-8')
idle = re.search(r'\.ai-box\s*\{(.*?)\n\}', css, re.S).group(1)
hover = re.search(r'\.ai-box:hover\s*\{(.*?)\}', css, re.S).group(1)
shadows = [re.search(r'box-shadow:\s*([^;]+)', rule).group(1) for rule in [idle, hover]]
cases = []
for width,height,radius in [(48,48,14),(89,54,14),(360,210,14),(360,272,14),(120,80,0)]:
    for state,shadow in enumerate(shadows):
        match = re.fullmatch(r'0 (\d+)px (\d+)px rgba\(0,0,0,([\d.]+)\)', shadow)
        offset,blur,opacity = match.groups()
        name=f'{width}x{height}-r{radius}-{state}'
        subprocess.run([str(pathlib.Path(a.probe).resolve()),str(width),str(height),str(radius),
                        blur,offset,opacity,str(out/(name+'.bmp'))],check=True,timeout=20)
        cases.append(dict(name=name,width=width,height=height,radius=radius,shadow=shadow))
runner = out/'capture.cjs'
runner.write_text("const {chromium}=require("+json.dumps(str(pathlib.Path(a.dependencies).resolve()/'node_modules/playwright-core'))+
    ");(async()=>{const browser=await chromium.launch({executablePath:"+json.dumps(a.browser)+
    ",headless:true});try{for(const c of "+json.dumps(cases)+"){const page=await browser.newPage({viewport:{width:c.width+80,height:c.height+80},deviceScaleFactor:1});await page.setContent(`<style>html,body{margin:0;background:white}.box{position:absolute;left:40px;top:40px;width:${c.width}px;height:${c.height}px;border-radius:${c.radius}px;background:rgba(255,255,255,.9);box-shadow:${c.shadow}}</style><div class=box></div>`);await page.screenshot({path:"+
    json.dumps(str(out))+"+'/'+c.name+'.png'});const png=require('node:fs').readFileSync("+json.dumps(str(out))+"+'/'+c.name+'.png');const rgba=await page.evaluate(async encoded=>{const image=new Image();image.src='data:image/png;base64,'+encoded;await image.decode();const canvas=document.createElement('canvas');canvas.width=image.width;canvas.height=image.height;const context=canvas.getContext('2d');context.drawImage(image,0,0);return Array.from(context.getImageData(0,0,image.width,image.height).data);},png.toString('base64'));require('node:fs').writeFileSync("+json.dumps(str(out))+"+'/'+c.name+'.rgba',Buffer.from(rgba));await page.close();}}finally{await browser.close();}})().catch(e=>{console.error(e);process.exit(1)});",encoding='utf-8')
subprocess.run(['node',str(runner)],check=True,timeout=90)
results=[]
for case in cases:
    bmp=(out/(case['name']+'.bmp')).read_bytes()
    width,height=struct.unpack_from('<ii',bmp,18)
    assert (width,-height) == (case['width']+80,case['height']+80)
    native=bmp[54:]
    reference=(out/(case['name']+'.rgba')).read_bytes()
    assert len(native) == len(reference) == width * -height * 4
    errors=[]
    for offset in range(0,len(native),4):
        got=(native[offset+2],native[offset+1],native[offset])
        want=tuple(reference[offset:offset+3])
        if got!=(255,255,255) or want!=(255,255,255):
            errors.append(max(abs(x-y) for x,y in zip(got,want)))
    errors.sort()
    result=dict(case=case['name'],mean=sum(errors)/len(errors),maximum=max(errors),p99=errors[int((len(errors)-1)*.99)],pixels=len(errors))
    results.append(result)
(out/'comparison.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
assert all(row['mean'] <= 2 and row['p99'] <= 4 for row in results), results
print('PASS',len(results),'CSS shadow pixel comparisons; alpha/cache buffers remain native')
