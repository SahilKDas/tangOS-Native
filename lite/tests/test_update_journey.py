"""Actual post-exit helper/restart checks; only disposable executable copies."""
import argparse, csv, ctypes, hashlib, io, json, os, pathlib, shutil, subprocess, sys, tempfile, time
from ctypes import wintypes as W
K=ctypes.WinDLL('kernel32',use_last_error=True); U=ctypes.WinDLL('user32',use_last_error=True)
K.CreateFileW.argtypes=[W.LPCWSTR,W.DWORD,W.DWORD,ctypes.c_void_p,W.DWORD,W.DWORD,W.HANDLE];K.CreateFileW.restype=W.HANDLE
K.TerminateProcess.argtypes=[W.HANDLE,W.UINT];K.CloseHandle.argtypes=[W.HANDLE];K.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];K.OpenProcess.restype=W.HANDLE
K.WaitForSingleObject.argtypes=[W.HANDLE,W.DWORD];K.WaitForSingleObject.restype=W.DWORD
U.GetWindowThreadProcessId.argtypes=[W.HWND,ctypes.POINTER(W.DWORD)]
U.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
CALLBACK=ctypes.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
U.EnumWindows.argtypes=[CALLBACK,W.LPARAM]
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def close_restarted(pid):
    handle=K.OpenProcess(0x100000|0x1000|1,False,pid)
    assert handle,'Restart process missing'
    try:
        deadline=time.monotonic()+20; windows=[]
        @CALLBACK
        def visit(hwnd,_):
            owner=W.DWORD();U.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
            if owner.value==pid:windows.append(hwnd)
            return True
        while not windows and time.monotonic()<deadline:
            U.EnumWindows(visit,0);time.sleep(.05)
        assert windows,'Restarted executable did not create a native window'
        for hwnd in windows:U.PostMessageW(hwnd,0x10,0,0)
        assert K.WaitForSingleObject(handle,15000)==0,'Restarted fixture did not close'
    finally:
        if K.WaitForSingleObject(handle,0)==258:K.TerminateProcess(handle,1);K.WaitForSingleObject(handle,5000)
        K.CloseHandle(handle)
def run(exe,mode):
    with tempfile.TemporaryDirectory(prefix='TangOS-update-journey-') as folder:
        root=pathlib.Path(folder); data=root/'profile'/'TangOSLite';store=data/'updates';store.mkdir(parents=True)
        target=root/'portable with spaces'/'TangOSLite.exe';target.parent.mkdir();shutil.copyfile(exe,target)
        original=digest(target);candidate=store/'candidate.exe';candidate.write_bytes(exe.read_bytes()+b'\nverified-disposable-update\n')
        config=data/'settings.ini';config.write_text('theme=2\nport_only=true\n');saved=config.read_bytes()
        receipt=store/'receipt.json';plan=dict(candidate=str(candidate),target=str(target),sha256=digest(candidate),targetSha256=original,version='99.0.0');receipt.write_text(json.dumps(plan))
        helper=store/'helper.exe';shutil.copyfile(exe,helper);lock=None;restart=mode in ('restart','restart-failure')
        if mode=='tampered':candidate.write_bytes(candidate.read_bytes()+b'tampered')
        if mode=='baseline':target.write_bytes(target.read_bytes()+b'user-changed')
        if mode=='restart-failure':
            user=subprocess.check_output(['whoami','/user','/fo','csv','/nh'],text=True)
            sid=next(csv.reader(io.StringIO(user)))[1]
            subprocess.run(['icacls',str(target),'/deny','*'+sid+':(X)'],capture_output=True,text=True,check=True)
        if mode=='locked':
            lock=K.CreateFileW(str(target),0x80000000,1,None,3,0,None)
            assert lock not in (None,ctypes.c_void_p(-1).value),'Fixture lock failed'
        before=digest(target);parent=subprocess.Popen([sys.executable,'-c','import time;time.sleep(1.5)'])
        env=dict(os.environ,LOCALAPPDATA=str(root/'profile'))
        child=subprocess.Popen([str(helper),'--apply-portable-update',str(receipt),str(parent.pid),'restart' if restart else 'quit'],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            deadline=time.monotonic()+1
            while time.monotonic()<deadline and parent.poll() is None:
                assert digest(target)==before,'Target replaced before parent exit';time.sleep(.03)
            parent.wait(timeout=5);out,err=child.communicate(timeout=35)
            result=json.loads((store/'last-result.json').read_text(encoding='utf-8-sig'))
            if mode=='restart-failure':
                assert child.returncode!=0 and result['state']=='installed' and result['restartState']=='error',(mode,result,err)
                assert digest(target)==plan['sha256'] and digest(pathlib.Path(result['backup']))==original
                assert result.get('message') and not receipt.exists(),'Restart failure lost installed/recovery state'
            elif mode in ('restart','quit'):
                assert child.returncode==0 and result['state']=='installed',(mode,result,err)
                assert digest(target)==plan['sha256'] and digest(pathlib.Path(result['backup']))==original
                assert not receipt.exists() and config.read_bytes()==saved
                if restart:
                    assert result['restartState']=='started';close_restarted(result['restartPid'])
            else:
                assert child.returncode!=0 and result['state']=='error',(mode,result)
                assert digest(target)==before and receipt.exists(),'Failed update damaged target/retry receipt'
        finally:
            if lock:K.CloseHandle(lock)
            if child.poll() is None:child.kill();child.wait()
            if parent.poll() is None:parent.kill();parent.wait()
    print('PASS actual update helper:',mode)
def main():
    p=argparse.ArgumentParser();p.add_argument('--exe',required=True);exe=pathlib.Path(p.parse_args().exe).resolve()
    for mode in ('quit','restart','restart-failure','locked','tampered','baseline'):run(exe,mode)
if __name__=='__main__':main()