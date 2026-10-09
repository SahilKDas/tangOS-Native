import json, pathlib, subprocess, tempfile, argparse
p=argparse.ArgumentParser();p.add_argument('--exe',required=True);exe=pathlib.Path(p.parse_args().exe).resolve()
with tempfile.TemporaryDirectory(prefix='TangOS-reviewed-git-') as root:
 root=pathlib.Path(root);repo=root/'repo';data=root/'data';repo.mkdir();data.mkdir();(repo/'port').mkdir();(repo/'src').mkdir()
 def git(*args):
  return subprocess.run(['git',*args],cwd=repo,text=True,capture_output=True,check=True).stdout
 git('init','-b','main');git('config','user.name','Disposable test');git('config','user.email','test@example.invalid')
 (repo/'port/fixture.cpp').write_text('base\n');(repo/'src/protected.cpp').write_text('unchanged\n')
 (repo/'tangos.json').write_text(json.dumps({'tangosVersion':'1','project':{'name':'git-fixture','title':'Git fixture'},'tools':[]}))
 git('add','.');git('commit','-m','base')
 def call(method,args):
  request=root/'request.json';response=root/'response.json';request.write_text(json.dumps({'method':method,'arguments':args}))
  result=subprocess.run([str(exe),'--backend',str(repo),str(data),str(request),str(response)],capture_output=True,text=True,timeout=30)
  value=json.loads(response.read_text());assert result.returncode==0,value;return value
 def confirmed(action,**args):
  args={'action':action,**args};preview=call('git.action',args);assert preview['requiresConfirmation'];args['confirmation']=preview['confirmation']
  result=call('git.action',args);assert result['exit']==0,result;return result
 confirmed('Create branch',ref='codex/new');confirmed('Switch branch',ref='codex/new');confirmed('Switch branch',ref='main');confirmed('Delete merged branch',ref='codex/new')
 (repo/'port/fixture.cpp').write_text('review before commit\n')
 confirmed('Stage paths',text='port/fixture.cpp')
 assert 'review before commit' in confirmed('Staged diff')['output']
 confirmed('Unstage paths',text='port/fixture.cpp')
 assert not git('diff','--cached') and (repo/'port/fixture.cpp').read_text()=='review before commit\n'
 assert 'review before commit' in confirmed('Working diff')['output']
 assert 'base' in confirmed('Commit history')['output']
 git('restore','port/fixture.cpp')
 confirmed('Create tag',ref='v1.0.0');assert git('tag').strip()=='v1.0.0';confirmed('Delete tag',ref='v1.0.0')
 (repo/'port/fixture.cpp').write_text('stashed\n');confirmed('Stash selected paths',text='port/fixture.cpp');assert (repo/'port/fixture.cpp').read_text()=='base\n'
 assert 'TangOS Lite' in confirmed('List stashes')['output'];confirmed('Apply stash',ref='stash@{0}');assert (repo/'port/fixture.cpp').read_text()=='stashed\n';confirmed('Drop stash',ref='stash@{0}')
 git('add','port/fixture.cpp');git('commit','-m','main update');git('branch','codex/conflict');git('switch','codex/conflict');(repo/'port/fixture.cpp').write_text('branch\n');git('commit','-am','branch update');git('switch','main');(repo/'port/fixture.cpp').write_text('main\n');git('commit','-am','main conflict')
 preview=call('git.action',{'action':'Merge','ref':'codex/conflict'});merged=call('git.action',{'action':'Merge','ref':'codex/conflict','confirmation':preview['confirmation']});assert merged['exit']!=0
 confirmed('Merge abort');assert (repo/'port/fixture.cpp').read_text()=='main\n'
 preview=call('git.action',{'action':'Rebase','ref':'codex/conflict'});rebased=call('git.action',{'action':'Rebase','ref':'codex/conflict','confirmation':preview['confirmation']});assert rebased['exit']!=0
 confirmed('Rebase abort');assert (repo/'port/fixture.cpp').read_text()=='main\n' and (repo/'src/protected.cpp').read_text()=='unchanged\n'
 print('PASS packaged reviewed Git: branch/switch/delete, tags, selected-path stash/apply/drop, conflict merge/rebase abort, protected source preserved')
