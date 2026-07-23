/**
 * @file         main.cpp
 * @author       eskeptor (yc.jeon)
 * @date         2026-07-23
 * @version      1.0.0
 */

#include <Windows.h>
#include <combaseapi.h>

#include "Application/CApplication.h"

int WINAPI wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE,
    _In_     LPWSTR,
    _In_     int
)
{
    // 모니터별 DPI 인식 설정 (Windows 10 1703+ 권장)
#if defined(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#else
    SetProcessDPIAware();
#endif

    // WGC(Windows Graphics Capture) API는 COM 초기화 필요
    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hrCo))
    {
        return -1;
    }

    CApplication cApp;
    int32_t nExitCode = -1;

    if (!cApp.Initialize(hInstance))
    {
        MessageBoxW(nullptr, L"Application initialization failed.", L"Error", MB_ICONERROR | MB_OK);
    }
    else
    {
        nExitCode = cApp.Run();
    }

    CoUninitialize();
    return nExitCode;
}
