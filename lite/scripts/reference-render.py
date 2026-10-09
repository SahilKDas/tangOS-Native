"""Build the retained original frontend with a read-only, offline fixture. Not shipped."""
import argparse,json,pathlib,shutil,subprocess

def main():
    root=pathlib.Path(__file__).resolve().parents[2]
    parser=argparse.ArgumentParser();parser.add_argument('--dependencies',type=pathlib.Path,default=root/'lite/out/reference-tools');parser.add_argument('--output',type=pathlib.Path,default=root/'lite/out/reference-ui');parser.add_argument('--interface-mode',choices=('simple','advanced'),default='simple');parser.add_argument('--fixture',type=pathlib.Path,default=root/'lite/tests/fixtures/reference-ui.json');args=parser.parse_args()
    deps=args.dependencies.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    values=json.loads(args.fixture.read_text(encoding='utf-8'))
    values['uiPrefsGet']['mode']=args.interface_mode
    bridge='window.__referenceCalls=[];const values='+json.dumps(values)+';window.tangos=new Proxy({}, {get:(_,name)=>{if(typeof name!=="string")return undefined;if(name.startsWith("on"))return ()=>()=>{};return ()=>{window.__referenceCalls.push(name);if(Object.hasOwn(values,name))return Promise.resolve(structuredClone(values[name]));return Promise.reject(new Error("Read-only visual fixture has no handler: "+name));};}});\n'
    (out/'fixture.js').write_text(bridge,encoding='utf-8')
    (out/'entry.tsx').write_text("import './fixture.js';import "+json.dumps((root/'console/src/renderer/src/main.tsx').as_posix())+';\n',encoding='utf-8')
    options={'entryPoints':[str(out/'entry.tsx')],'outfile':str(out/'app.js'),'bundle':True,'jsx':'automatic','define':{'import.meta.env.DEV':'false'},'nodePaths':[str(deps/'node_modules')],'alias':{'@tangos/ui/aero.css':str(root/'packages/ui/aero.css')},'loader':{'.png':'file'},'assetNames':'assets/[name]-[hash]','logLevel':'warning'}
    builder='const esbuild=require('+json.dumps(str(deps/'node_modules/esbuild'))+');esbuild.buildSync('+json.dumps(options)+');'
    (out/'build.cjs').write_text(builder,encoding='utf-8');subprocess.run(['node',str(out/'build.cjs')],check=True)
    shutil.copyfile(root/'lite/assets/Nunito.ttf',out/'Nunito.ttf')
    (out/'index.html').write_text("""<!doctype html><html><head><meta charset="utf-8"><meta http-equiv="Content-Security-Policy" content="default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; font-src 'self'; connect-src 'none'"><title>Original Console - local visual reference</title><link rel="stylesheet" href="app.css"><style>@font-face{font-family:Nunito;src:url(Nunito.ttf);font-weight:100 900}body{font-family:Nunito,sans-serif}</style></head><body><div id="root"></div><script src="app.js"></script></body></html>""",encoding='utf-8')
    print('Read-only reference renderer:',out)
if __name__=='__main__':main()
