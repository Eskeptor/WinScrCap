/**
 * @file         AgentConfig.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-08
 * @version      1.0.0
 */

#pragma once

#include <Windows.h>
#include <cstdint>

constexpr uint16_t AGENT_DEFAULT_PORT = 9201;
constexpr int32_t AGENT_DEFAULT_FPS = 30;
constexpr int32_t AGENT_DEFAULT_CRF = 18;

struct SAgentConfig
{
    uint16_t Port = AGENT_DEFAULT_PORT;
    char szPassword[128] = {};
    int32_t Fps = AGENT_DEFAULT_FPS;
    int32_t Crf = AGENT_DEFAULT_CRF;
    int32_t MonitorIndex = 0;
};

bool LoadAgentConfig(const wchar_t* lpszIniPath, SAgentConfig* pOutConfig);
bool SaveAgentConfig(const wchar_t* lpszIniPath, const SAgentConfig& stConfig);
void GetAgentIniPath(wchar_t* lpszPathOut, int32_t nMaxLen);
