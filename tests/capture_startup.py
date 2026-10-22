"""Capture exceptions from one explicitly launched x86 player; never attach to other processes."""
import argparse
import ctypes as c
import ctypes.wintypes as w
import json
from pathlib import Path
import struct
import subprocess
import time

k = c.WinDLL('kernel32', use_last_error=True)
class SI(c.Structure):
    _fields_ = [('cb',w.DWORD),('reserved',w.LPWSTR),('desktop',w.LPWSTR),('title',w.LPWSTR),('x',w.DWORD),('y',w.DWORD),('xs',w.DWORD),('ys',w.DWORD),('xc',w.DWORD),('yc',w.DWORD),('fill',w.DWORD),('flags',w.DWORD),('show',w.WORD),('cbreserved',w.WORD),('reserved2',c.c_void_p),('input',w.HANDLE),('output',w.HANDLE),('error',w.HANDLE)]
class PI(c.Structure):
    _fields_ = [('process',w.HANDLE),('thread',w.HANDLE),('pid',w.DWORD),('tid',w.DWORD)]
class Event(c.Structure):
    _fields_ = [('code',w.DWORD),('pid',w.DWORD),('tid',w.DWORD),('padding',w.DWORD),('data',c.c_ubyte*160)]
k.CreateProcessW.argtypes = [w.LPCWSTR,w.LPWSTR,c.c_void_p,c.c_void_p,w.BOOL,w.DWORD,c.c_void_p,w.LPCWSTR,c.POINTER(SI),c.POINTER(PI)]
k.WaitForDebugEvent.argtypes = [c.POINTER(Event),w.DWORD]
k.ContinueDebugEvent.argtypes = [w.DWORD,w.DWORD,w.DWORD]
k.ReadProcessMemory.argtypes = [w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
k.OpenThread.argtypes = [w.DWORD,w.BOOL,w.DWORD];k.OpenThread.restype = w.HANDLE
k.Wow64GetThreadContext.argtypes = [w.HANDLE,c.c_void_p]
k.GetFinalPathNameByHandleW.argtypes = [w.HANDLE,w.LPWSTR,w.DWORD,w.DWORD]
k.CloseHandle.argtypes = [w.HANDLE];k.TerminateProcess.argtypes = [w.HANDLE,w.UINT]
u=c.WinDLL('user32',use_last_error=True)
WindowCallback=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
u.EnumWindows.argtypes=[WindowCallback,w.LPARAM]
u.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)]
u.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,c.c_int]
u.IsWindowVisible.argtypes=[w.HWND];u.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]

def main():
    parser=argparse.ArgumentParser();parser.add_argument('exe');parser.add_argument('--output',required=True);parser.add_argument('--seconds',type=float,default=12)
    parser.add_argument('--smoke',action='store_true');parser.add_argument('--close-after',type=float);parser.add_argument('--visible',action='store_true');args=parser.parse_args()
    executable=Path(args.exe).resolve();out=Path(args.output).resolve();out.parent.mkdir(parents=True,exist_ok=True)
    si=SI();si.cb=c.sizeof(si);si.flags=1;si.show=1 if args.visible else 0;pi=PI()
    command=c.create_unicode_buffer(subprocess.list2cmdline([str(executable)]+(['--smoke-test'] if args.smoke else [])))
    if not k.CreateProcessW(None,command,None,None,False,2|0x08000000,None,str(executable.parent),c.byref(si),c.byref(pi)):raise c.WinError(c.get_last_error())
    modules={};exceptions=[];exit_code=None;terminated=False;windows=[];close_sent=False
    def read(address,count):
        buffer=c.create_string_buffer(count);size=c.c_size_t();k.ReadProcessMemory(pi.process,address,buffer,count,c.byref(size));return buffer.raw[:size.value]
    def describe(address):
        for base,(name,size) in modules.items():
            if base<=address<base+size:return name+'+'+hex(address-base)
        return hex(address)
    started=time.monotonic();deadline=started+args.seconds
    while time.monotonic()<deadline:
        if args.close_after is not None and not close_sent and time.monotonic()-started>=args.close_after:
            @WindowCallback
            def collect(hwnd,_):
                pid=w.DWORD();u.GetWindowThreadProcessId(hwnd,c.byref(pid))
                if pid.value==pi.pid:
                    name=c.create_unicode_buffer(256);u.GetClassNameW(hwnd,name,256)
                    windows.append({'class':name.value,'visible':bool(u.IsWindowVisible(hwnd))})
                    if name.value=='TTPlayer_PlayerWnd':u.PostMessageW(hwnd,0x10,0,0)
                return True
            u.EnumWindows(collect,0);close_sent=True
        event=Event()
        if not k.WaitForDebugEvent(c.byref(event),100):continue
        data=bytes(event.data);status=0x10002
        if event.code in (3,6):
            handle=struct.unpack_from('<Q',data,0)[0];base=struct.unpack_from('<Q',data,24 if event.code==3 else 8)[0];name=c.create_unicode_buffer(32768)
            if handle:k.GetFinalPathNameByHandleW(handle,name,32768,0);k.CloseHandle(handle)
            dos=read(base,64);size=0
            if len(dos)==64:
                pe=read(base+struct.unpack_from('<I',dos,60)[0],84)
                if len(pe)==84:size=struct.unpack_from('<I',pe,80)[0]
            modules[base]=(name.value,size)
        elif event.code==7:modules.pop(struct.unpack_from('<Q',data,0)[0],None)
        elif event.code==1:
            code=struct.unpack_from('<I',data,0)[0];address=struct.unpack_from('<Q',data,16)[0];chance=struct.unpack_from('<I',data,152)[0]
            if code not in (0x80000003,0x4000001f):
                status=0x80010001
                if code in (0xc0000005,0xc000041d,0xc00000fd,0xc0000409) or not chance:
                    thread=k.OpenThread(0x1f03ff,False,event.tid);ctx=c.create_string_buffer(716);struct.pack_into('<I',ctx,0,0x10007)
                    k.Wow64GetThreadContext(thread,ctx);k.CloseHandle(thread)
                    regs={name:struct.unpack_from('<I',ctx,off)[0] for name,off in [('eax',176),('ecx',172),('edx',168),('ebx',164),('esi',160),('edi',156),('ebp',180),('eip',184),('esp',196)]}
                    stack=read(regs['esp'],384);stack=stack[:len(stack)//4*4]
                    parameters=struct.unpack_from('<15Q',data,32)[:min(15,struct.unpack_from('<I',data,24)[0])]
                    item={'code':hex(code),'address':describe(address),'first_chance':chance,'parameters':[hex(p) for p in parameters],
                          'parameter_modules':[describe(p) for p in parameters], 'registers':{n:hex(v) for n,v in regs.items()},
                          'instructions':read(regs['eip']-16,48).hex(),'stack':[describe(p) for p in struct.unpack('<'+'I'*(len(stack)//4),stack)]}
                    exceptions.append(item);print(json.dumps({key:item[key] for key in ('code','address','first_chance','parameter_modules')}),flush=True)
        elif event.code==5:exit_code=struct.unpack_from('<I',data,0)[0]
        k.ContinueDebugEvent(event.pid,event.tid,status)
        if exit_code is not None:break
    if exit_code is None:
        terminated=True;k.TerminateProcess(pi.process,0)
        for _ in range(50):
            event=Event()
            if k.WaitForDebugEvent(c.byref(event),100):
                k.ContinueDebugEvent(event.pid,event.tid,0x10002)
                if event.code==5:break
    result={'exe':str(executable),'pid':pi.pid,'smoke':args.smoke,'exit_code':exit_code,'alive_until_timeout':terminated,'close_requested':close_sent,'windows_at_close':windows,'exceptions':exceptions,'modules':{hex(base):value for base,value in modules.items()}}
    out.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');k.CloseHandle(pi.process);k.CloseHandle(pi.thread)
    print(json.dumps({key:result[key] for key in ('exit_code','alive_until_timeout')})+' exceptions='+str(len(exceptions)))
    return 1 if exceptions or (exit_code not in (None,0)) else 0
if __name__=='__main__':raise SystemExit(main())
