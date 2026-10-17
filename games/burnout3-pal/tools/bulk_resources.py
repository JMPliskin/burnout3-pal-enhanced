"""Local Windows resource telemetry; no external dependencies.

Reductions drain existing tasks before replacing the pool. Never kill a task
merely to resize. Paging counters are explicitly unavailable when PDH fails.
"""
import ctypes as c
from ctypes import wintypes as w
import os
import time


def limit(current,sample):
    pressure=sample['ram_percent']>=85 or sample.get('paging_pressure',False) or sample.get('resource_error',False)
    return (6 if current>6 else 4 if current>4 else current) if pressure else current


def paging_pressure(pages,ram,duration):
    return duration>=10 and pages is not None and (pages>=10000 or ram>=80 and pages>=1000)


class Memory(c.Structure):
    _fields_=[('length',w.DWORD),('load',w.DWORD)]+[(n,c.c_ulonglong) for n in
       ('total','available','total_page','available_page','total_virtual','available_virtual','extended')]


class ValueUnion(c.Union):
    _fields_=[('value',c.c_double),('integer',c.c_longlong)]


class CounterValue(c.Structure):
    _fields_=[('status',w.DWORD),('data',ValueUnion)]


class Monitor:
    def __init__(self):
        self.kernel=c.WinDLL('kernel32',use_last_error=True)
        process_mask=c.c_size_t();system_mask=c.c_size_t()
        self.kernel.GetCurrentProcess.restype=w.HANDLE
        self.kernel.GetProcessAffinityMask.argtypes=[w.HANDLE,c.POINTER(c.c_size_t),c.POINTER(c.c_size_t)]
        ok=self.kernel.GetProcessAffinityMask(self.kernel.GetCurrentProcess(),c.byref(process_mask),c.byref(system_mask))
        self.cpu_count=process_mask.value.bit_count() if ok else os.cpu_count() or 1
        self.previous=None;self.query=w.HANDLE();self.counter=w.HANDLE();self.paging_since=None;self.last_sample=None;self.last_sample_time=0
        self.pdh=c.WinDLL('pdh')
        self.pdh.PdhOpenQueryW.argtypes=[w.LPCWSTR,c.c_size_t,c.POINTER(w.HANDLE)]
        self.pdh.PdhAddEnglishCounterW.argtypes=[w.HANDLE,w.LPCWSTR,c.c_size_t,c.POINTER(w.HANDLE)]
        self.pdh.PdhCollectQueryData.argtypes=[w.HANDLE]
        self.pdh.PdhGetFormattedCounterValue.argtypes=[w.HANDLE,w.DWORD,c.POINTER(w.DWORD),c.POINTER(CounterValue)]
        self.pdh.PdhCloseQuery.argtypes=[w.HANDLE]
        self.paging_available=False
        if self.pdh.PdhOpenQueryW(None,0,c.byref(self.query))==0:
            self.paging_available=self.pdh.PdhAddEnglishCounterW(self.query,r'\Memory\Pages Input/sec',0,c.byref(self.counter))==0
            if self.paging_available:self.pdh.PdhCollectQueryData(self.query)

    def sample(self):
        stamp=time.monotonic()
        if self.last_sample is not None and stamp-self.last_sample_time<2:return dict(self.last_sample)
        memory=Memory();memory.length=c.sizeof(memory)
        if not self.kernel.GlobalMemoryStatusEx(c.byref(memory)):raise c.WinError(c.get_last_error())
        idle=c.c_ulonglong();kernel=c.c_ulonglong();user=c.c_ulonglong()
        if not self.kernel.GetSystemTimes(c.byref(idle),c.byref(kernel),c.byref(user)):raise c.WinError(c.get_last_error())
        now=(idle.value,kernel.value+user.value);cpu=None
        if self.previous:
            total=now[1]-self.previous[1]
            if total:cpu=100*(1-(now[0]-self.previous[0])/total)
        self.previous=now;pages=None
        if self.paging_available and self.pdh.PdhCollectQueryData(self.query)==0:
            value=CounterValue()
            if self.pdh.PdhGetFormattedCounterValue(self.counter,0x200,None,c.byref(value))==0 and value.status in (0,1):pages=value.data.value
        elevated=pages is not None and (pages>=10000 or memory.load>=80 and pages>=1000)
        self.paging_since=(self.paging_since if self.paging_since is not None else stamp) if elevated else None
        duration=stamp-self.paging_since if self.paging_since is not None else 0
        self.last_sample_time=stamp
        self.last_sample={'ram_percent':memory.load,'available_bytes':memory.available,'cpu_percent':cpu,
                'pages_input_per_second':pages,'paging_pressure':paging_pressure(pages,memory.load,duration),'paging_duration_seconds':duration,
                'paging_counter_available':self.paging_available}
        return dict(self.last_sample)

    def close(self):
        if self.query:self.pdh.PdhCloseQuery(self.query);self.query=w.HANDLE()
