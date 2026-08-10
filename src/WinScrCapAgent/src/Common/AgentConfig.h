/**
 * @file         AgentConfig.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-08
 * @version      1.0.0
 * @brief        Definition of the agent configuration structure and INI load/save functions
 */

#pragma once

#include <Windows.h>
#include <cstdint>

constexpr uint16_t AGENT_DEFAULT_PORT = 9201;      /**< Default listen port for the agent server */
constexpr int32_t AGENT_DEFAULT_FPS = 30;          /**< Default capture/encode frame rate */
constexpr int32_t AGENT_DEFAULT_CRF = 18;          /**< Default H.264 CRF value */

/**
 * @struct   SAgentConfig
 * @brief    Structure holding the persisted agent configuration
 */
struct SAgentConfig
{
    uint16_t Port = AGENT_DEFAULT_PORT;    /**< TCP port the agent server listens on */
    char szPassword[128] = {};             /**< Authentication password (plain text, used for challenge-response) */
    int32_t Fps = AGENT_DEFAULT_FPS;       /**< Capture/encode frame rate */
    int32_t Crf = AGENT_DEFAULT_CRF;       /**< H.264 CRF value */
    int32_t MonitorIndex = 0;              /**< Index of the monitor to capture */
};

/**
 * @brief       Loads the agent configuration from an INI file.
 * @param[in]   lpszIniPath     Path to the INI file to read
 * @param[out]  pOutConfig      Pointer to the SAgentConfig receiving the loaded values
 * @return      Whether the configuration was loaded successfully
 */
bool LoadAgentConfig(const wchar_t* lpszIniPath, SAgentConfig* pOutConfig);

/**
 * @brief       Saves the agent configuration to an INI file.
 * @param[in]   lpszIniPath     Path to the INI file to write
 * @param[in]   stConfig        Configuration values to save
 * @return      Whether the configuration was saved successfully
 */
bool SaveAgentConfig(const wchar_t* lpszIniPath, const SAgentConfig& stConfig);

/**
 * @brief       Builds the full path to the agent's INI configuration file.
 * @param[out]  lpszPathOut     Buffer receiving the resulting path string
 * @param[in]   nMaxLen         Size of lpszPathOut in characters
 */
void GetAgentIniPath(wchar_t* lpszPathOut, int32_t nMaxLen);
