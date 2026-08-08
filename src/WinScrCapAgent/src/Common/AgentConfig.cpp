/**
 * @file         AgentConfig.cpp
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-08
 * @version      1.0.0
 */

#include <Windows.h>
#include <cassert>
#include <cstdio>

#include "AgentConfig.h"

bool LoadAgentConfig(const wchar_t* lpszIniPath, SAgentConfig* pOutConfig)
{
    assert(lpszIniPath != nullptr);
    assert(pOutConfig != nullptr);

    pOutConfig->Port = static_cast<uint16_t>(
        GetPrivateProfileIntW(L"Agent", L"Port", AGENT_DEFAULT_PORT, lpszIniPath));

    wchar_t szPwd[128] = {};
    GetPrivateProfileStringW(L"Agent", L"Password", L"", szPwd, ARRAYSIZE(szPwd), lpszIniPath);
    WideCharToMultiByte(CP_UTF8, 0, szPwd, -1,
        pOutConfig->szPassword, static_cast<int32_t>(sizeof(pOutConfig->szPassword)),
        nullptr, nullptr);

    pOutConfig->Fps = GetPrivateProfileIntW(L"Agent", L"FPS", AGENT_DEFAULT_FPS, lpszIniPath);
    pOutConfig->Crf = GetPrivateProfileIntW(L"Agent", L"CRF", AGENT_DEFAULT_CRF, lpszIniPath);
    pOutConfig->MonitorIndex = GetPrivateProfileIntW(L"Agent", L"MonitorIndex", 0, lpszIniPath);

    if (pOutConfig->Fps < 1)
    {
        pOutConfig->Fps = 1;
    }
    if (pOutConfig->Fps > 60)
    {
        pOutConfig->Fps = 60;
    }
    if (pOutConfig->Crf < 0)
    {
        pOutConfig->Crf = 0;
    }
    if (pOutConfig->Crf > 51)
    {
        pOutConfig->Crf = 51;
    }
    if (pOutConfig->MonitorIndex < 0)
    {
        pOutConfig->MonitorIndex = 0;
    }

    return true;
}

bool SaveAgentConfig(const wchar_t* lpszIniPath, const SAgentConfig& stConfig)
{
    assert(lpszIniPath != nullptr);

    wchar_t szBuf[64] = {};

    _snwprintf_s(szBuf, ARRAYSIZE(szBuf), _TRUNCATE, L"%u", static_cast<uint32_t>(stConfig.Port));
    WritePrivateProfileStringW(L"Agent", L"Port", szBuf, lpszIniPath);

    wchar_t szPwd[128] = {};
    MultiByteToWideChar(CP_UTF8, 0, stConfig.szPassword, -1, szPwd, ARRAYSIZE(szPwd));
    WritePrivateProfileStringW(L"Agent", L"Password", szPwd, lpszIniPath);

    _snwprintf_s(szBuf, ARRAYSIZE(szBuf), _TRUNCATE, L"%d", stConfig.Fps);
    WritePrivateProfileStringW(L"Agent", L"FPS", szBuf, lpszIniPath);

    _snwprintf_s(szBuf, ARRAYSIZE(szBuf), _TRUNCATE, L"%d", stConfig.Crf);
    WritePrivateProfileStringW(L"Agent", L"CRF", szBuf, lpszIniPath);

    _snwprintf_s(szBuf, ARRAYSIZE(szBuf), _TRUNCATE, L"%d", stConfig.MonitorIndex);
    WritePrivateProfileStringW(L"Agent", L"MonitorIndex", szBuf, lpszIniPath);

    return true;
}

void GetAgentIniPath(wchar_t* lpszPathOut, int32_t nMaxLen)
{
    GetModuleFileNameW(nullptr, lpszPathOut, static_cast<DWORD>(nMaxLen));
    wchar_t* pszDot = wcsrchr(lpszPathOut, L'.');
    if (pszDot != nullptr)
    {
        const ptrdiff_t nRemaining = nMaxLen - static_cast<ptrdiff_t>(pszDot - lpszPathOut);
        wcscpy_s(pszDot, static_cast<rsize_t>(nRemaining), L".ini");
    }
}
