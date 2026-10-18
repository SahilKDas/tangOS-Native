"""Compare numeric update eligibility against retained Console updater code."""
import argparse,http.server,json,pathlib,re,subprocess,tempfile,threading

def main():
 p=argparse.ArgumentParser();p.add_argument('--exe',required=True);exe=pathlib.Path(p.parse_args().exe).resolve();root=pathlib.Path(__file__).resolve().parents[2]
 current=re.search(r'project\(TangOSLite VERSION ([0-9.]+)',(root/'lite/CMakeLists.txt').read_text()).group(1)
 original=(root/'console/src/main/updater.ts').read_text();body=re.search(r'^function isNewer\([\s\S]*?^\}',original,re.M).group(0)
 cases=['0.1.0','0.37.0',current,'0.38.1','0.100.0','1.0.0','1.0.0.0','10.2.3','0.0.0']
 state={'version':current};requests=[]
 class Handler(http.server.BaseHTTPRequestHandler):
  def log_message(self,*args):pass
  def do_GET(self):
   requests.append(self.path);self.send_response(200);self.end_headers();self.wfile.write(json.dumps({'version':state['version'],'html_url':'https://example.test/releases'}).encode())
 server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
 try:
  with tempfile.TemporaryDirectory(prefix='TangOS-update-reference-') as folder:
   tmp=pathlib.Path(folder);data=tmp/'state';data.mkdir();script=tmp/'reference.ts'
   script.write_text(body+'\nconsole.log(JSON.stringify('+json.dumps(cases)+'.map(v=>isNewer(v,'+json.dumps(current)+'))));')
   reference=subprocess.run(['node',str(script)],capture_output=True,text=True,timeout=20,check=True);expected=json.loads(reference.stdout)
   (data/'connections.json').write_text(json.dumps({'update.check':{'enabled':True,'url':f'http://127.0.0.1:{server.server_port}/release','automatic':False}}))
   def call(method,args):
    req=tmp/'request.json';out=tmp/'response.json';req.write_text(json.dumps({'method':method,'arguments':args}))
    result=subprocess.run([str(exe),'--backend','-',str(data),str(req),str(out)],capture_output=True,timeout=20);assert result.returncode==0,result.stderr
    return json.loads(out.read_text(encoding='utf-8-sig'))
   for version,want in zip(cases,expected):
    state['version']=version;actual=call('update.check',{})['update'];assert (actual['state']=='available')==want,(version,actual,want)
   assert len(requests)==len(cases) and not (data/'updates').exists(),'Checking eligibility downloaded/staged an update'
   patch={'reports':True,'safeMode':True,'unrelatedFutureSetting':{'retain':1}}
   preview=call('preferences.set',patch);call('preferences.set',dict(patch,confirmation=preview['confirmation']))
   preview=call('preferences.set',{'reports':False});call('preferences.set',{'reports':False,'confirmation':preview['confirmation']})
   restored=call('preferences.get',{});assert not restored['reports'] and restored['safeMode'] and restored['unrelatedFutureSetting']=={'retain':1}
 finally:server.shutdown();server.server_close();thread.join()
 print(f'PASS {len(cases)} original updater eligibility comparisons; check-only network behavior and restart-safe preference merging')
if __name__=='__main__':main()