// FPS Booster Pro - real hardware monitoring engine (Windows 7+).
// Every value comes from a genuine API. Anything the OS cannot provide
// returns -1 (numbers) or "" (strings) and the UI shows N/A - never fake data.
//
// Sources:
//   CPU %     GetSystemTimes (via CpuMeter, all Windows)
//   CPU MHz   CallNtPowerInformation/ProcessorInformation (powrprof, Win7+)
//   CPU temp  WMI MSAcpi_ThermalZoneTemperature (often absent -> N/A)
//   GPU %     NVML dGPU (NVIDIA) -> PDH GPU Engine sum (Win10+) -> ADL (AMD)
//   GPU temp/clock/power/fan/mem  NVML -> ADL (temp/activity only)
//   GPU name/VRAM  DXGI (all Windows)
//   GPU driver  display-class registry (matched to DXGI name)
//   RAM       GlobalMemoryStatusEx + GetPerformanceInfo (cached/commit)
//   Disk %    PDH \PhysicalDisk(_Total)\% Disk Time
//   Net up/down  GetIfEntry2 byte deltas on up adapters
//   Ping      ICMP echo to 1.1.1.1 in a worker thread (never blocks UI)
//   CPU cores/threads  GetLogicalProcessorInformation
//   RAM speed/type  WMI Win32_PhysicalMemory (worker thread)
//   Display   EnumDisplaySettings (+ IDXGIOutput6 HDR probe, Win10+)
#include <winsock2.h>
#include <ws2tcpip.h>
#include "app.h"
#include <psapi.h>
#include <powrprof.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <icmpapi.h>
#include <dxgi.h>
#if defined(__has_include)
#if __has_include(<dxgi1_6.h>)
#include <dxgi1_6.h>
#define MON_HAVE_DXGI16 1
#endif
#endif
#include <objbase.h>
#include <oaidl.h>
#include <oleauto.h>
#include <wbemidl.h>

// WMI GUIDs defined locally so no wbemuuid.lib is needed.
// {4590F811-1D3A-11D0-891F-00AA004B2E24}
static const CLSID MON_CLSID_WbemLocator =
    {0x4590f811, 0x1d3a, 0x11d0, {0x89, 0x1f, 0x00, 0xaa, 0x00, 0x4b, 0x2e, 0x24}};
// {DC12A687-737F-11CF-884D-00AA004B2E24}
static const IID MON_IID_WbemLocator =
    {0xdc12a687, 0x737f, 0x11cf, {0x88, 0x4d, 0x00, 0xaa, 0x00, 0x4b, 0x2e, 0x24}};

#define MON_HIST 90

// ---------- sample store ----------
static int g_cpu = 0, g_cpuMhz = -1, g_cpuBaseMhz = 0;
static int g_gpu = -1, g_gpuTemp = -1, g_gpuClock = -1, g_gpuPower = -1, g_gpuFan = -1;
static int g_gpuMemUsed = -1, g_gpuMemTotal = -1;
static int g_ram = 0, g_ramAvailMB = 0, g_ramCachedMB = -1;
static int g_disk = -1, g_downKB = 0, g_upKB = 0;
static volatile LONG g_pingMs = -1, g_cpuTemp = -1, g_ramSpeed = -1, g_ramType = 0;
static int g_cores = 0, g_threads = 0, g_logical = 0;
static std::wstring g_gpuDriver;
static int g_hdr = -1; // -1 unknown, 0 SDR, 1 HDR
static int g_histN = 0;
static int g_hCpu[MON_HIST], g_hGpu[MON_HIST], g_hRam[MON_HIST];
static int g_hDisk[MON_HIST], g_hNet[MON_HIST], g_hPing[MON_HIST];
static CpuMeter* g_monMeter = NULL;
static bool g_monInit = false;

static void HistPush(int h[MON_HIST], int v) {
    if (g_histN < MON_HIST) { h[g_histN] = v; }
    else { memmove(h, h + 1, sizeof(int) * (MON_HIST - 1)); h[MON_HIST - 1] = v; }
}

// ---------- dynamic PDH (no link dependency) ----------
struct PdhFmtVal { DWORD CStatus; union { LONG l; double d; }; };
typedef void* PDHQ;
typedef void* PDHC;
typedef LONG (WINAPI* PdhOpenQueryWFn)(LPCWSTR, DWORD_PTR, PDHQ*);
typedef LONG (WINAPI* PdhAddCounterWFn)(PDHQ, LPCWSTR, DWORD_PTR, PDHC*);
typedef LONG (WINAPI* PdhCollectQueryDataFn)(PDHQ);
typedef LONG (WINAPI* PdhGetFormattedCounterValueFn)(PDHC, DWORD, DWORD*, PdhFmtVal*);
typedef LONG (WINAPI* PdhCloseQueryFn)(PDHQ);
typedef LONG (WINAPI* PdhEnumObjectItemsWFn)(LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, DWORD*,
                                             LPWSTR, DWORD*, DWORD, DWORD);
#define PDH_FMT_DOUBLE_VAL 0x200u
#define PDH_MORE_DATA_VAL ((LONG)0x800007D2)
static HMODULE g_pdh = NULL;
static PdhOpenQueryWFn pPdhOpenQueryW = NULL;
static PdhAddCounterWFn pPdhAddCounterW = NULL;
static PdhCollectQueryDataFn pPdhCollectQueryData = NULL;
static PdhGetFormattedCounterValueFn pPdhGetFormattedCounterValue = NULL;
static PdhCloseQueryFn pPdhCloseQuery = NULL;
static PdhEnumObjectItemsWFn pPdhEnumObjectItemsW = NULL;
static PDHQ g_pdhQ = NULL;
static PDHC g_pdhDisk = NULL;
static std::vector<PDHC> g_pdhGpu;
static int g_pdhGpuReenum = 0;

static bool PdhLoad() {
    if (g_pdh) return true;
    g_pdh = LoadLibraryW(L"pdh.dll");
    if (!g_pdh) return false;
    pPdhOpenQueryW = (PdhOpenQueryWFn)GetProcAddress(g_pdh, "PdhOpenQueryW");
    pPdhAddCounterW = (PdhAddCounterWFn)GetProcAddress(g_pdh, "PdhAddCounterW");
    pPdhCollectQueryData = (PdhCollectQueryDataFn)GetProcAddress(g_pdh, "PdhCollectQueryData");
    pPdhGetFormattedCounterValue =
        (PdhGetFormattedCounterValueFn)GetProcAddress(g_pdh, "PdhGetFormattedCounterValue");
    pPdhCloseQuery = (PdhCloseQueryFn)GetProcAddress(g_pdh, "PdhCloseQuery");
    pPdhEnumObjectItemsW = (PdhEnumObjectItemsWFn)GetProcAddress(g_pdh, "PdhEnumObjectItemsW");
    if (!pPdhOpenQueryW || !pPdhAddCounterW || !pPdhCollectQueryData ||
        !pPdhGetFormattedCounterValue || !pPdhCloseQuery)
        return false;
    if (pPdhOpenQueryW(NULL, 0, &g_pdhQ) != ERROR_SUCCESS || !g_pdhQ) return false;
    pPdhAddCounterW(g_pdhQ, L"\\PhysicalDisk(_Total)\\% Disk Time", 0, &g_pdhDisk);
    pPdhCollectQueryData(g_pdhQ);
    return true;
}
static double PdhRead(PDHC c, bool& ok) {
    ok = false;
    if (!c || !pPdhGetFormattedCounterValue) return 0;
    PdhFmtVal v; v.CStatus = 0; v.d = 0;
    if (pPdhGetFormattedCounterValue(c, PDH_FMT_DOUBLE_VAL, NULL, &v) != ERROR_SUCCESS) return 0;
    if (v.CStatus != ERROR_SUCCESS) return 0;
    ok = true;
    return v.d;
}
static void PdhGpuEnum() {
    g_pdhGpu.clear();
    if (!pPdhEnumObjectItemsW || !g_pdhQ) return;
    DWORD cLen = 0, iLen = 0;
    LONG r = pPdhEnumObjectItemsW(NULL, NULL, L"GPU Engine", NULL, &cLen, NULL, &iLen, 400, 0);
    if (r != PDH_MORE_DATA_VAL || iLen < 2) return;
    std::vector<wchar_t> buf(iLen + 2, 0);
    cLen = 0; DWORD il = iLen;
    if (pPdhEnumObjectItemsW(NULL, NULL, L"GPU Engine", NULL, &cLen, buf.data(), &il, 400, 0)
        != ERROR_SUCCESS)
        return;
    for (wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) {
        std::wstring path = L"\\GPU Engine(" + std::wstring(p) + L")\\Utilization Percentage";
        PDHC c = NULL;
        if (pPdhAddCounterW(g_pdhQ, path.c_str(), 0, &c) == ERROR_SUCCESS && c)
            g_pdhGpu.push_back(c);
        if (g_pdhGpu.size() >= 64) break;
    }
    pPdhCollectQueryData(g_pdhQ);
}

// ---------- NVML (NVIDIA, dynamic) ----------
#define NVML_SUCCESS_VAL 0
struct NvmlUtil { unsigned int gpu, memory; };
struct NvmlMem { unsigned long long total, free, used; };
typedef int (*NvmlInitFn)();
typedef int (*NvmlShutdownFn)();
typedef int (*NvmlCountFn)(unsigned int*);
typedef int (*NvmlHandleFn)(unsigned int, void**);
typedef int (*NvmlUtilFn)(void*, NvmlUtil*);
typedef int (*NvmlTempFn)(void*, int, unsigned int*);
typedef int (*NvmlMemFn)(void*, NvmlMem*);
typedef int (*NvmlClockFn)(void*, int, unsigned int*);
typedef int (*NvmlPowerFn)(void*, unsigned int*);
typedef int (*NvmlFanFn)(void*, unsigned int*);
static HMODULE g_nvml = NULL;
static void* g_nvmlDev = NULL;
static NvmlUtilFn pNvmlUtil = NULL;
static NvmlTempFn pNvmlTemp = NULL;
static NvmlMemFn pNvmlMem = NULL;
static NvmlClockFn pNvmlClock = NULL;
static NvmlPowerFn pNvmlPower = NULL;
static NvmlFanFn pNvmlFan = NULL;
static NvmlShutdownFn pNvmlShutdown = NULL;
static bool NvmlLoad() {
    if (g_nvml) return g_nvmlDev != NULL;
    g_nvml = LoadLibraryW(L"nvml.dll");
    if (!g_nvml) return false;
    NvmlInitFn init = (NvmlInitFn)GetProcAddress(g_nvml, "nvmlInit_v2");
    if (!init) init = (NvmlInitFn)GetProcAddress(g_nvml, "nvmlInit");
    NvmlCountFn count = (NvmlCountFn)GetProcAddress(g_nvml, "nvmlDeviceGetCount_v2");
    if (!count) count = (NvmlCountFn)GetProcAddress(g_nvml, "nvmlDeviceGetCount");
    NvmlHandleFn hand = (NvmlHandleFn)GetProcAddress(g_nvml, "nvmlDeviceGetHandleByIndex_v2");
    if (!hand) hand = (NvmlHandleFn)GetProcAddress(g_nvml, "nvmlDeviceGetHandleByIndex");
    pNvmlUtil = (NvmlUtilFn)GetProcAddress(g_nvml, "nvmlDeviceGetUtilizationRates");
    pNvmlTemp = (NvmlTempFn)GetProcAddress(g_nvml, "nvmlDeviceGetTemperature");
    pNvmlMem = (NvmlMemFn)GetProcAddress(g_nvml, "nvmlDeviceGetMemoryInfo");
    pNvmlClock = (NvmlClockFn)GetProcAddress(g_nvml, "nvmlDeviceGetClockInfo");
    pNvmlPower = (NvmlPowerFn)GetProcAddress(g_nvml, "nvmlDeviceGetPowerUsage");
    pNvmlFan = (NvmlFanFn)GetProcAddress(g_nvml, "nvmlDeviceGetFanSpeed");
    pNvmlShutdown = (NvmlShutdownFn)GetProcAddress(g_nvml, "nvmlShutdown");
    if (!init || init() != NVML_SUCCESS_VAL) return false;
    unsigned int n = 0;
    if (!count || !hand || count(&n) != NVML_SUCCESS_VAL || n == 0) return false;
    if (hand(0, &g_nvmlDev) != NVML_SUCCESS_VAL || !g_nvmlDev) return false;
    return true;
}

// ---------- ADL (AMD, dynamic, Overdrive5) ----------
struct AdlPMActivity {
    int iSize, iEngineClock, iMemoryClock, iVddc, iActivityPercent,
        iCurrentPerformanceLevel, iCurrentBusSpeed, iCurrentBusLanes, iMaximumBusLanes;
};
typedef void* (*AdlMallocFn)(int);
typedef int (*AdlCreateFn)(AdlMallocFn, int);
typedef int (*AdlDestroyFn)();
typedef int (*AdlAdaptersFn)(int*);
typedef int (*AdlActivityFn)(int, int, AdlPMActivity*);
typedef int (*AdlTempFn)(int, int, int*);
static void* AdlAlloc(int s) { return malloc(s); }
static HMODULE g_adl = NULL;
static int g_adlIdx = -1;
static AdlActivityFn pAdlActivity = NULL;
static AdlTempFn pAdlTemp = NULL;
static bool AdlLoad() {
    if (g_adl) return g_adlIdx >= 0;
    g_adl = LoadLibraryW(L"atiadlxx.dll");
    if (!g_adl) g_adl = LoadLibraryW(L"atiadlxy.dll");
    if (!g_adl) return false;
    AdlCreateFn create = (AdlCreateFn)GetProcAddress(g_adl, "ADL_Main_Control_Create");
    AdlAdaptersFn nad = (AdlAdaptersFn)GetProcAddress(g_adl, "ADL_Adapter_NumberOfAdapters_Get");
    pAdlActivity = (AdlActivityFn)GetProcAddress(g_adl, "ADL_Overdrive5_CurrentActivity_Get");
    pAdlTemp = (AdlTempFn)GetProcAddress(g_adl, "ADL_Overdrive5_Temperature_Get");
    if (!create || create(AdlAlloc, 1) != 0) return false;
    int n = 0;
    if (!nad || nad(&n) != 0 || n <= 0) return false;
    // Probe adapters 0..n-1 for one that answers Overdrive5.
    for (int i = 0; i < n && i < 16; i++) {
        if (pAdlActivity) {
            AdlPMActivity a; memset(&a, 0, sizeof(a)); a.iSize = sizeof(a);
            if (pAdlActivity(i, 0, &a) == 0 && a.iActivityPercent >= 0 &&
                a.iActivityPercent <= 100) {
                g_adlIdx = i;
                return true;
            }
        }
        if (pAdlTemp) {
            int t = 0;
            if (pAdlTemp(i, 0, &t) == 0 && t > 0 && t < 125000) {
                g_adlIdx = i;
                return true;
            }
        }
    }
    return false;
}

// ---------- WMI (slow thread only) ----------
static bool WmiUlong(IWbemServices* svc, const wchar_t* q, const wchar_t* prop, ULONG& out) {
    IEnumWbemClassObject* en = NULL;
    BSTR ql = SysAllocString(L"WQL"), qq = SysAllocString(q);
    HRESULT hr = svc->ExecQuery(ql, qq,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &en);
    SysFreeString(ql); SysFreeString(qq);
    if (FAILED(hr) || !en) return false;
    bool ok = false;
    for (;;) {
        IWbemClassObject* obj = NULL; ULONG ret = 0;
        if (en->Next(WBEM_INFINITE, 1, &obj, &ret) != S_OK || !ret || !obj) break;
        VARIANT v; VariantInit(&v);
        if (obj->Get(prop, 0, &v, NULL, NULL) == S_OK) {
            if (v.vt == VT_I4) { out = (ULONG)v.lVal; ok = true; }
            else if (v.vt == VT_UI4) { out = v.ulVal; ok = true; }
            else if (v.vt == VT_I2) { out = (ULONG)v.iVal; ok = true; }
            else if (v.vt == VT_UI2) { out = v.uiVal; ok = true; }
            else if (v.vt == VT_BSTR && v.bstrVal) { out = (ULONG)_wtoi(v.bstrVal); ok = true; }
        }
        VariantClear(&v); obj->Release();
        if (ok) break;
    }
    en->Release();
    return ok;
}
static IWbemServices* WmiConnect(IWbemLocator* loc, const wchar_t* ns) {
    if (!loc) return NULL;
    IWbemServices* svc = NULL;
    BSTR bns = SysAllocString(ns);
    HRESULT hr = loc->ConnectServer(bns, NULL, NULL, NULL, 0, NULL, NULL, &svc);
    SysFreeString(bns);
    if (FAILED(hr) || !svc) return NULL;
    CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    return svc;
}

// ---------- slow worker: ping + CPU temp + RAM speed/type ----------
static HANDLE g_slowThread = NULL;
static volatile LONG g_slowQuit = 0;
static int PingOnce(HANDLE hIcmp) {
    if (hIcmp == INVALID_HANDLE_VALUE || !hIcmp) return -1;
    IN_ADDR a; a.S_un.S_addr = 0;
    if (InetPtonW(AF_INET, L"1.1.1.1", &a) != 1) return -1;
    char send[32]; memset(send, 'M', sizeof(send));
    char reply[sizeof(ICMP_ECHO_REPLY) + 64];
    DWORD r = IcmpSendEcho(hIcmp, a.S_un.S_addr, send, sizeof(send), NULL,
                           reply, sizeof(reply), 900);
    if (r == 0) return -1;
    ICMP_ECHO_REPLY* e = (ICMP_ECHO_REPLY*)reply;
    if (e->Status != IP_SUCCESS) return -1;
    return (int)e->RoundTripTime;
}
static DWORD WINAPI MonSlowThread(LPVOID) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    HANDLE hIcmp = IcmpCreateFile();
    IWbemLocator* loc = NULL;
    CoCreateInstance(MON_CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER,
                     MON_IID_WbemLocator, (void**)&loc);
    IWbemServices* svcCim = loc ? WmiConnect(loc, L"ROOT\\CIMV2") : NULL;
    IWbemServices* svcWmi = loc ? WmiConnect(loc, L"ROOT\\WMI") : NULL;
    int cycle = 0;
    while (!InterlockedCompareExchange(&g_slowQuit, 0, 0)) {
        InterlockedExchange(&g_pingMs, PingOnce(hIcmp));
        if ((cycle % 3) == 0) {
            // CPU temperature (tenths of Kelvin -> C), best effort.
            ULONG t = 0;
            int c = -1;
            if (svcWmi && WmiUlong(svcWmi,
                    L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature",
                    L"CurrentTemperature", t)) {
                int cc = (int)(t / 10.0 - 273.15 + 0.5);
                if (cc >= 0 && cc <= 125) c = cc;
            }
            InterlockedExchange(&g_cpuTemp, c);
            // RAM speed/type once (retry until known).
            if (InterlockedCompareExchange(&g_ramSpeed, 0, 0) <= 0 && svcCim) {
                ULONG sp = 0, ty = 0;
                if (WmiUlong(svcCim, L"SELECT Speed FROM Win32_PhysicalMemory",
                             L"Speed", sp) && sp > 0 && sp < 20000)
                    InterlockedExchange(&g_ramSpeed, (LONG)sp);
                if (WmiUlong(svcCim, L"SELECT SMBIOSMemoryType FROM Win32_PhysicalMemory",
                             L"SMBIOSMemoryType", ty) && ty > 0)
                    InterlockedExchange(&g_ramType, (LONG)ty);
            }
        }
        cycle++;
        for (int i = 0; i < 10 && !InterlockedCompareExchange(&g_slowQuit, 0, 0); i++)
            Sleep(500);
    }
    if (svcCim) svcCim->Release();
    if (svcWmi) svcWmi->Release();
    if (loc) loc->Release();
    if (hIcmp && hIcmp != INVALID_HANDLE_VALUE) IcmpCloseHandle(hIcmp);
    CoUninitialize();
    return 0;
}

// ---------- CPU topology + clocks ----------
static int PopCountPtr(ULONG_PTR m) {
    int n = 0;
    while (m) { n += (int)(m & 1); m >>= 1; }
    return n;
}
static void CpuTopology() {
    DWORD len = 0;
    GetLogicalProcessorInformation(NULL, &len);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || len == 0) return;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION* b =
        (SYSTEM_LOGICAL_PROCESSOR_INFORMATION*)malloc(len);
    if (!b) return;
    if (GetLogicalProcessorInformation(b, &len)) {
        int cores = 0, threads = 0;
        DWORD n = len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        for (DWORD i = 0; i < n; i++) {
            if (b[i].Relationship == RelationProcessorCore) {
                cores++;
                threads += PopCountPtr((ULONG_PTR)b[i].ProcessorMask);
            }
        }
        if (cores > 0) { g_cores = cores; g_threads = threads; g_logical = threads; }
    }
    free(b);
}
struct ProcPowerInfoMin { ULONG Number, MaxMhz, CurrentMhz, MhzLimit, MaxIdleState; };
typedef LONG (WINAPI* CallNtPowerInfoFn)(INT, PVOID, ULONG, PVOID, ULONG);
static CallNtPowerInfoFn pPowerInfo = NULL;
static int CpuCurrentMhz() {
    if (!pPowerInfo) {
        HMODULE m = GetModuleHandleW(L"powrprof.dll");
        if (!m) m = LoadLibraryW(L"powrprof.dll");
        if (m) pPowerInfo = (CallNtPowerInfoFn)GetProcAddress(m, "CallNtPowerInformation");
        if (!pPowerInfo) return -1;
    }
    int n = g_logical > 0 ? g_logical : 64;
    if (n > 256) n = 256;
    std::vector<ProcPowerInfoMin> v(n);
    // level 11 = ProcessorInformation
    if (pPowerInfo(11, NULL, 0, v.data(), (ULONG)(n * sizeof(ProcPowerInfoMin))) != 0)
        return -1;
    ULONGLONG sum = 0; int c = 0;
    for (int i = 0; i < n; i++)
        if (v[i].CurrentMhz > 0 && v[i].CurrentMhz < 20000) { sum += v[i].CurrentMhz; c++; }
    return c ? (int)(sum / (ULONGLONG)c) : -1;
}

// ---------- DXGI: GPU name / VRAM / HDR ----------
static void DxgiInfo(std::wstring& name, int& vramMB, int& hdr) {
    name.clear(); vramMB = -1; hdr = -1;
    IDXGIFactory* f = NULL;
    if (FAILED(CreateDXGIFactory(IID_IDXGIFactory, (void**)&f)) || !f) return;
    ULONGLONG best = 0;
    for (UINT i = 0;; i++) {
        IDXGIAdapter* a = NULL;
        if (f->EnumAdapters(i, &a) != S_OK || !a) break;
        DXGI_ADAPTER_DESC d; memset(&d, 0, sizeof(d));
        if (a->GetDesc(&d) == S_OK && d.VendorId != 0x1414 &&
            d.DedicatedVideoMemory > best) {
            best = d.DedicatedVideoMemory;
            name = d.Description;
        }
        a->Release();
    }
    if (best) vramMB = (int)(best >> 20);
#ifdef MON_HAVE_DXGI16
    // HDR probe on first output of first adapter (Win10+, fails cleanly elsewhere).
    IDXGIAdapter* a0 = NULL;
    if (f->EnumAdapters(0, &a0) == S_OK && a0) {
        IDXGIOutput* o = NULL;
        if (a0->EnumOutputs(0, &o) == S_OK && o) {
            static const IID IID_IDXGIOutput6 =
                {0x068346e8, 0xaaec, 0x4b84,
                 {0xad, 0xd7, 0x13, 0x7f, 0x51, 0x3f, 0x77, 0xa1}};
            IDXGIOutput6* o6 = NULL;
            if (o->QueryInterface(IID_IDXGIOutput6, (void**)&o6) == S_OK && o6) {
                DXGI_OUTPUT_DESC1 d1; memset(&d1, 0, sizeof(d1));
                if (o6->GetDesc1(&d1) == S_OK)
                    hdr = (d1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
                           d1.ColorSpace == DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020)
                              ? 1
                              : 0;
                o6->Release();
            }
            o->Release();
        }
        a0->Release();
    }
#endif
    f->Release();
}
static std::wstring GpuDriverVersion(const std::wstring& gpuName) {
    std::wstring ver, first;
    const wchar_t* base =
        L"SYSTEM\\CurrentControlSet\\Control\\Class\\"
        L"{4d36e968-e325-11ce-bfc1-08002be10318}";
    for (int i = 0; i < 16; i++) {
        wchar_t sub[300];
        swprintf(sub, 300, L"%s\\%04d", base, i);
        std::wstring desc, vv;
        if (!RegGetString(HKEY_LOCAL_MACHINE, sub, L"DriverVersion", vv) || vv.empty())
            continue;
        bool sane = false;
        for (size_t k = 0; k < vv.size(); k++)
            if (vv[k] >= L'0' && vv[k] <= L'9') { sane = true; break; }
        if (!sane) continue;
        if (first.empty()) first = vv;
        if (RegGetString(HKEY_LOCAL_MACHINE, sub, L"DriverDesc", desc) && !desc.empty() &&
            !gpuName.empty()) {
            if (gpuName.find(desc) != std::wstring::npos ||
                desc.find(gpuName) != std::wstring::npos)
                return vv;
            // token match (first word, e.g. "NVIDIA")
            size_t sp = gpuName.find(L' ');
            std::wstring tok = gpuName.substr(0, sp == std::wstring::npos ? 6 : sp);
            if (!tok.empty() && desc.find(tok) != std::wstring::npos) return vv;
        }
    }
    return first;
}

// ---------- network rates ----------
static ULONGLONG g_netIn = 0, g_netOut = 0;
static DWORD g_netTick = 0;
static bool g_netHave = false;
static ULONGLONG g_diskFreeGB = 0;
static void NetSample() {
    ULONG sz = 15000;
    IP_ADAPTER_ADDRESSES* a = (IP_ADAPTER_ADDRESSES*)malloc(sz);
    if (!a) return;
    DWORD r = GetAdaptersAddresses(AF_UNSPEC, 0, NULL, a, &sz);
    if (r == ERROR_BUFFER_OVERFLOW) {
        free(a);
        a = (IP_ADAPTER_ADDRESSES*)malloc(sz);
        if (!a) return;
        r = GetAdaptersAddresses(AF_UNSPEC, 0, NULL, a, &sz);
    }
    if (r == ERROR_SUCCESS) {
        ULONGLONG in = 0, out = 0;
        for (IP_ADAPTER_ADDRESSES* p = a; p; p = p->Next) {
            if (p->OperStatus != IfOperStatusUp) continue;
            if (p->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            MIB_IF_ROW2 row; memset(&row, 0, sizeof(row));
            row.InterfaceIndex = p->IfIndex;
            if (GetIfEntry2(&row) != NO_ERROR) continue;
            in += row.InOctets; out += row.OutOctets;
        }
        DWORD now = GetTickCount();
        if (g_netHave) {
            DWORD dt = now - g_netTick;
            if (dt >= 500 && dt <= 10000) {
                g_downKB = (int)((in - g_netIn) * 1000 / dt / 1024);
                g_upKB = (int)((out - g_netOut) * 1000 / dt / 1024);
                if (g_downKB < 0) g_downKB = 0;
                if (g_upKB < 0) g_upKB = 0;
            }
        }
        g_netIn = in; g_netOut = out; g_netTick = now; g_netHave = true;
    }
    free(a);
}

// ---------- public API ----------
void MonInit() {
    if (g_monInit) return;
    g_monInit = true;
    for (int i = 0; i < MON_HIST; i++)
        g_hCpu[i] = g_hGpu[i] = g_hRam[i] = g_hDisk[i] = g_hNet[i] = g_hPing[i] = -1;
    CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT,
                         RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
    CpuTopology();
    DWORD mhz = 0;
    if (RegGetDword(HKEY_LOCAL_MACHINE,
                    L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz", mhz) &&
        mhz > 0)
        g_cpuBaseMhz = (int)mhz;
    std::wstring gname;
    DxgiInfo(gname, g_gpuMemTotal, g_hdr);
    if (gname.empty()) gname = SysGpuName();
    g_gpuDriver = GpuDriverVersion(gname);
    PdhLoad();
    PdhGpuEnum();
    NvmlLoad();
    AdlLoad();
    NetSample();
    // total free space on fixed drives (GB)
    DWORD drives = GetLogicalDrives();
    ULONGLONG freeB = 0;
    for (int i = 0; i < 26; i++) {
        if (!(drives & (1u << i))) continue;
        wchar_t root[4] = {(wchar_t)(L'A' + i), L':', L'\\', 0};
        if (GetDriveTypeW(root) != DRIVE_FIXED) continue;
        ULARGE_INTEGER fr, tot, totFree;
        if (GetDiskFreeSpaceExW(root, &fr, &tot, &totFree)) freeB += fr.QuadPart;
    }
    g_diskFreeGB = freeB >> 30;
    g_slowThread = CreateThread(NULL, 0, MonSlowThread, NULL, 0, NULL);
    MonSample();
}
void MonShutdown() {
    InterlockedExchange(&g_slowQuit, 1);
    if (g_slowThread) {
        WaitForSingleObject(g_slowThread, 2500);
        CloseHandle(g_slowThread);
        g_slowThread = NULL;
    }
    if (g_pdhQ && pPdhCloseQuery) { pPdhCloseQuery(g_pdhQ); g_pdhQ = NULL; }
    if (g_nvml && pNvmlShutdown) pNvmlShutdown();
    if (g_nvml) { FreeLibrary(g_nvml); g_nvml = NULL; }
    if (g_adl) {
        AdlDestroyFn d = (AdlDestroyFn)GetProcAddress(g_adl, "ADL_Main_Control_Destroy");
        if (d) d();
        FreeLibrary(g_adl);
        g_adl = NULL;
    }
    if (g_pdh) { FreeLibrary(g_pdh); g_pdh = NULL; }
    delete g_monMeter; g_monMeter = NULL;
}
void MonSample() {
    if (!g_monInit) return;
    if (!g_monMeter) g_monMeter = new CpuMeter();
    g_cpu = g_monMeter->sample();
    if (g_cpu < 0) g_cpu = 0;
    if (g_cpu > 100) g_cpu = 100;
    int mhz = CpuCurrentMhz();
    if (mhz > 0) g_cpuMhz = mhz;
    g_ram = RamUsagePercent();
    g_ramAvailMB = RamAvailMB();
    PERFORMANCE_INFORMATION pi; memset(&pi, 0, sizeof(pi));
    pi.cb = sizeof(pi);
    if (GetPerformanceInfo(&pi, sizeof(pi)) && pi.PageSize)
        g_ramCachedMB = (int)((ULONGLONG)pi.SystemCache * pi.PageSize >> 20);
    // GPU: NVML dGPU first, then PDH engine sum, then ADL.
    g_gpu = -1; g_gpuTemp = -1; g_gpuClock = -1; g_gpuPower = -1; g_gpuFan = -1;
    g_gpuMemUsed = -1;
    if (g_nvmlDev) {
        if (pNvmlUtil) {
            NvmlUtil u; memset(&u, 0, sizeof(u));
            if (pNvmlUtil(g_nvmlDev, &u) == NVML_SUCCESS_VAL) g_gpu = (int)u.gpu;
        }
        unsigned int t = 0;
        if (pNvmlTemp && pNvmlTemp(g_nvmlDev, 0, &t) == NVML_SUCCESS_VAL && t < 130)
            g_gpuTemp = (int)t;
        if (pNvmlClock && pNvmlClock(g_nvmlDev, 0, &t) == NVML_SUCCESS_VAL && t > 0)
            g_gpuClock = (int)t;
        if (pNvmlPower && pNvmlPower(g_nvmlDev, &t) == NVML_SUCCESS_VAL)
            g_gpuPower = (int)(t / 1000);
        if (pNvmlFan && pNvmlFan(g_nvmlDev, &t) == NVML_SUCCESS_VAL && t <= 100)
            g_gpuFan = (int)t;
        if (pNvmlMem) {
            NvmlMem m; memset(&m, 0, sizeof(m));
            if (pNvmlMem(g_nvmlDev, &m) == NVML_SUCCESS_VAL) {
                g_gpuMemUsed = (int)(m.used >> 20);
                if (g_gpuMemTotal <= 0) g_gpuMemTotal = (int)(m.total >> 20);
            }
        }
    }
    if (g_gpu < 0 && g_pdhQ && pPdhCollectQueryData) {
        if (++g_pdhGpuReenum >= 30) {
            g_pdhGpuReenum = 0;
            // drop engine counters and re-enumerate (processes come and go)
            PdhGpuEnum();
        }
        pPdhCollectQueryData(g_pdhQ);
        double sum = 0; int good = 0;
        for (size_t i = 0; i < g_pdhGpu.size(); i++) {
            bool ok = false;
            double v = PdhRead(g_pdhGpu[i], ok);
            if (ok) { sum += v; good++; }
        }
        if (good > 0) {
            if (sum > 100) sum = 100;
            if (sum < 0) sum = 0;
            g_gpu = (int)(sum + 0.5);
        }
    }
    if (g_adlIdx >= 0) {
        if (g_gpu < 0 && pAdlActivity) {
            AdlPMActivity a; memset(&a, 0, sizeof(a)); a.iSize = sizeof(a);
            if (pAdlActivity(g_adlIdx, 0, &a) == 0 && a.iActivityPercent >= 0 &&
                a.iActivityPercent <= 100)
                g_gpu = a.iActivityPercent;
        }
        if (g_gpuTemp < 0 && pAdlTemp) {
            int t = 0;
            if (pAdlTemp(g_adlIdx, 0, &t) == 0 && t > 0 && t < 125000)
                g_gpuTemp = t / 1000;
        }
    }
    // disk
    g_disk = -1;
    if (g_pdhQ && g_pdhDisk && pPdhCollectQueryData) {
        pPdhCollectQueryData(g_pdhQ);
        bool ok = false;
        double v = PdhRead(g_pdhDisk, ok);
        if (ok) {
            if (v > 100) v = 100;
            if (v < 0) v = 0;
            g_disk = (int)(v + 0.5);
        }
    }
    NetSample();
    int ping = InterlockedCompareExchange(&g_pingMs, 0, 0);
    HistPush(g_hCpu, g_cpu);
    HistPush(g_hGpu, g_gpu);
    HistPush(g_hRam, g_ram);
    HistPush(g_hDisk, g_disk);
    HistPush(g_hNet, g_downKB);
    HistPush(g_hPing, ping);
    if (g_histN < MON_HIST) g_histN++;
}

int MonCpu() { return g_cpu; }
int MonCpuMhz() { return g_cpuMhz; }
int MonCpuBaseMhz() { return g_cpuBaseMhz; }
int MonCpuTempC() { return InterlockedCompareExchange(&g_cpuTemp, 0, 0); }
int MonCpuCores() { return g_cores; }
int MonCpuThreads() { return g_threads; }
int MonGpu() { return g_gpu; }
int MonGpuTempC() { return g_gpuTemp; }
int MonGpuClockMhz() { return g_gpuClock; }
int MonGpuPowerW() { return g_gpuPower; }
int MonGpuFanPct() { return g_gpuFan; }
int MonGpuMemUsedMB() { return g_gpuMemUsed; }
int MonGpuMemTotalMB() { return g_gpuMemTotal; }
std::wstring MonGpuDriver() { return g_gpuDriver; }
int MonRam() { return g_ram; }
int MonRamAvailMB() { return g_ramAvailMB; }
int MonRamCachedMB() { return g_ramCachedMB; }
int MonRamSpeedMHz() { return InterlockedCompareExchange(&g_ramSpeed, 0, 0); }
std::wstring MonRamType() {
    switch (InterlockedCompareExchange(&g_ramType, 0, 0)) {
    case 24: return L"DDR3";
    case 26: return L"DDR4";
    case 27: return L"LPDDR4";
    case 34: return L"DDR5";
    case 35: return L"LPDDR5";
    default: return L"";
    }
}
int MonDisk() { return g_disk; }
int MonDiskFreeGB() { return (int)g_diskFreeGB; }
int MonDownKBs() { return g_downKB; }
int MonUpKBs() { return g_upKB; }
int MonPingMs() { return InterlockedCompareExchange(&g_pingMs, 0, 0); }

int MonHistN() { return g_histN; }
int MonHistGet(int id, int* out90) {
    const int* h = g_hCpu;
    if (id == 1) h = g_hGpu;
    else if (id == 2) h = g_hRam;
    else if (id == 3) h = g_hDisk;
    else if (id == 4) h = g_hNet;
    else if (id == 5) h = g_hPing;
    for (int i = 0; i < g_histN; i++) out90[i] = h[i];
    return g_histN;
}
void MonMinMaxAvg(int id, int* mn, int* mx, int* avg) {
    const int* h = g_hCpu;
    if (id == 1) h = g_hGpu;
    else if (id == 2) h = g_hRam;
    else if (id == 3) h = g_hDisk;
    else if (id == 4) h = g_hNet;
    else if (id == 5) h = g_hPing;
    int a = -1, b = -1; long long s = 0; int n = 0;
    for (int i = 0; i < g_histN; i++) {
        if (h[i] < 0) continue;
        if (a < 0 || h[i] < a) a = h[i];
        if (h[i] > b) b = h[i];
        s += h[i]; n++;
    }
    *mn = a; *mx = b; *avg = n ? (int)(s / n) : -1;
}
std::wstring MonDrives() {
    std::wstring s;
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (!(drives & (1u << i))) continue;
        wchar_t root[4] = {(wchar_t)(L'A' + i), L':', L'\\', 0};
        if (GetDriveTypeW(root) != DRIVE_FIXED) continue;
        ULARGE_INTEGER fr, tot, totFree;
        if (!GetDiskFreeSpaceExW(root, &fr, &tot, &totFree)) continue;
        if (!s.empty()) s += L", ";
        s += WFormat(L"%c: %d/%d GB", L'A' + i, (int)(fr.QuadPart >> 30),
                     (int)(tot.QuadPart >> 30));
    }
    return s.empty() ? L"?" : s;
}
std::wstring MonDisplay() {
    DEVMODEW dm; memset(&dm, 0, sizeof(dm)); dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm)) return L"?";
    std::wstring s = WFormat(L"%dx%d @ %d Hz", (int)dm.dmPelsWidth,
                             (int)dm.dmPelsHeight, (int)dm.dmDisplayFrequency);
    if (g_hdr == 1) s += L" HDR";
    return s;
}
