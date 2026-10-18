"""Compare packaged GUI preference transitions with original Console IPC handlers."""
import argparse,json,pathlib,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--trace',required=True);args=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[2];trace=json.loads(pathlib.Path(args.trace).read_text(encoding='utf-8-sig'));source=(root/'console/src/main/index.ts').read_text()
chunks=[]
for name in ('ui:setPrefs','bg:setPrefs'):
 start=source.index("ipcMain.handle('"+name+"'");end=source.index('\n})',start)+3;chunks.append(source[start:end])
with tempfile.TemporaryDirectory(prefix='TangOS-preferences-reference-') as folder:
 script=pathlib.Path(folder)/'reference.ts'
 script.write_text("type UiPrefs={mode:'simple'|'advanced'};type BackgroundPrefs={enabled:boolean};const trace="+json.dumps(trace)+";let uiPrefs:UiPrefs={mode:trace.initialMode};let bgPrefs:BackgroundPrefs={enabled:trace.initialBackground};const handlers=new Map();const ipcMain={handle:(name,fn)=>handlers.set(name,fn)};const saveSettings=()=>{};\n"+'\n'.join(chunks)+"\nconsole.log(JSON.stringify({modeStates:trace.modeRequests.map(mode=>handlers.get('ui:setPrefs')(null,{mode}).mode),backgroundStates:trace.backgroundRequests.map(enabled=>handlers.get('bg:setPrefs')(null,{enabled}).enabled)}));")
 result=subprocess.run(['node',str(script)],capture_output=True,text=True,timeout=20,check=True);reference=json.loads(result.stdout)
 for key,value in reference.items():assert trace[key]==value,(key,trace[key],value)
print('PASS original Console mode/background preference transitions; native durable publication and reopen checked by GUI fixture')