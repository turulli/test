/*
 *      Copyright (C) 2005-2010 Team XBMC
 *      http://www.xbmc.org
 *
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#ifdef HAS_DS_PLAYER

#include "MadvrCallback.h"
#include "cores/DSPlayer/Filters/MadvrSettingsManager.h"

CMadvrCallback *CMadvrCallback::m_pSingleton = NULL;

CMadvrCallback::CMadvrCallback()
{
  m_pAllocatorCallback = NULL;  
  m_pSettingCallback = NULL;
  m_pPaintCallback = NULL;
  m_renderOnMadvr = false;
  ResetRenderCount();
  m_currentVideoLayer = RENDER_LAYER_UNDER;
}

CMadvrCallback::~CMadvrCallback()
{
  m_pAllocatorCallback = NULL;
  m_pSettingCallback = NULL;
  m_pPaintCallback = NULL;
}

CMadvrCallback* CMadvrCallback::Get()
{
  return (m_pSingleton) ? m_pSingleton : (m_pSingleton = new CMadvrCallback());
}

void CMadvrCallback::IncRenderCount()
{ 
  if (!ReadyMadvr())
    return;

  m_currentVideoLayer == RENDER_LAYER_UNDER ? m_renderUnderCount += 1 : m_renderOverCount += 1;
}

void CMadvrCallback::ResetRenderCount()
{
  m_renderUnderCount = 0;  
  m_renderOverCount = 0;
}

bool CMadvrCallback::GuiVisible(MADVR_RENDER_LAYER layer)
{
  bool result = false;
  switch (layer)
  {
  case RENDER_LAYER_UNDER:
    result = m_renderUnderCount > 0;
    break;
  case RENDER_LAYER_OVER:
    result = m_renderOverCount > 0;
    break;
  case RENDER_LAYER_ALL:
    result = m_renderOverCount + m_renderUnderCount > 0;
    break;
  }
  return result;
}

bool CMadvrCallback::UsingMadvr()
{
  return (m_pAllocatorCallback != NULL);
}

bool CMadvrCallback::ReadyMadvr()
{
  return (m_pAllocatorCallback != NULL && m_renderOnMadvr);
}

// IMadvrAllocatorCallback
bool CMadvrCallback::IsEnteringExclusive()
{ 
  if (UsingMadvr())
    return m_pAllocatorCallback->IsEnteringExclusive();

  return false;
}

void CMadvrCallback::EnableExclusive(bool bEnable)
{
  if (UsingMadvr())
    m_pAllocatorCallback->EnableExclusive(bEnable);
}

void CMadvrCallback::SetMadvrPixelShader()
{
  if (UsingMadvr())
    m_pAllocatorCallback->SetMadvrPixelShader();
}

bool CMadvrCallback::ParentWindowProc(HWND hWnd, UINT uMsg, WPARAM *wParam, LPARAM *lParam, LRESULT *ret) 
{ 
  if (UsingMadvr())
    return m_pAllocatorCallback->ParentWindowProc(hWnd,uMsg,wParam,lParam,ret);

  return false; 
}

void CMadvrCallback::SetMadvrPosition(CRect wndRect, CRect videoRect) 
{
  if (UsingMadvr())
    m_pAllocatorCallback->SetMadvrPosition(wndRect, videoRect);
}

// IMadvrPaintCallback
void CMadvrCallback::RenderToUnderTexture()
{
  if (m_pPaintCallback && ReadyMadvr())
    m_pPaintCallback->RenderToUnderTexture();
}

void CMadvrCallback::RenderToOverTexture()
{
  if (m_pPaintCallback && ReadyMadvr())
    m_pPaintCallback->RenderToOverTexture();
}

void CMadvrCallback::EndRender()
{
  if (m_pPaintCallback && ReadyMadvr())
    m_pPaintCallback->EndRender();
}

// IMadvrSettingCallback
void CMadvrCallback::RestoreSettings()
{
  if (m_pSettingCallback)
    m_pSettingCallback->RestoreSettings();
}

void CMadvrCallback::LoadSettings(MADVR_LOAD_TYPE type)
{
  if (m_pSettingCallback)
    m_pSettingCallback->LoadSettings(type);
}

void CMadvrCallback::GetProfileActiveName(std::string *profile)
{
  if (m_pSettingCallback)
    m_pSettingCallback->GetProfileActiveName(profile);
}

void CMadvrCallback::SetStr(std::string path, std::string sValue) 
{
  if (m_pSettingCallback)
    m_pSettingCallback->SetStr(path, sValue);
}

void CMadvrCallback::SetBool(std::string path, bool bValue) 
{
  if (m_pSettingCallback)
    m_pSettingCallback->SetBool(path, bValue);
};
void CMadvrCallback::SetInt(std::string path, int iValue) 
{
  if (m_pSettingCallback)
    m_pSettingCallback->SetInt(path, iValue);
}

void CMadvrCallback::SetFloat(std::string path, float fValue, int iConv) 
{
  if (m_pSettingCallback)
    m_pSettingCallback->SetFloat(path, fValue, iConv);
}

void CMadvrCallback::SetDoubling(std::string path, int iValue) 
{
  if (m_pSettingCallback)
    m_pSettingCallback->SetDoubling(path, iValue);
}

void CMadvrCallback::SetDeintActive(std::string path, int iValue) 
{
  if (m_pSettingCallback)
    m_pSettingCallback->SetDeintActive(path, iValue);
}

void CMadvrCallback::SetBoolValue(std::string path, std::string sValue, int iValue)
{
  if (m_pSettingCallback)
    m_pSettingCallback->SetBoolValue(path, sValue, iValue);
}

void CMadvrCallback::SetMultiBool(std::string path, std::string sValue, int iValue)
{
  if (m_pSettingCallback)
    m_pSettingCallback->SetMultiBool(path, sValue, iValue);
}

void CMadvrCallback::SetSmoothmotion(std::string path, int iValue) 
{
  if (m_pSettingCallback)
    m_pSettingCallback->SetSmoothmotion(path, iValue);
}

void CMadvrCallback::SetDithering(std::string path, int iValue) 
{
  if (m_pSettingCallback)
    m_pSettingCallback->SetDithering(path, iValue);
}

std::string CMadvrCallback::GetSettingsName(MADVR_SETTINGS_LIST type, int iValue)
{
  if (m_pSettingCallback)
    return m_pSettingCallback->GetSettingsName(type, iValue);

  return "";
}

void CMadvrCallback::AddEntry(MADVR_SETTINGS_LIST type, StaticIntegerSettingOptions *entry)
{
  if (m_pSettingCallback)
    m_pSettingCallback->AddEntry(type, entry);
}

void CMadvrCallback::UpdateImageDouble()
{
  if (m_pSettingCallback)
    m_pSettingCallback->UpdateImageDouble();
}

#endif