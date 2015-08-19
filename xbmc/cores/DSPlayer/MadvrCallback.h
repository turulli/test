#pragma once
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

#ifndef HAS_DS_PLAYER
#error DSPlayer's header file included without HAS_DS_PLAYER defined
#endif

#include "DSUtil/DSUtil.h"
#include "DSUtil/SmartPtr.h"
#include "IPaintCallback.h"
#include "streams.h"
#include "utils/CharsetConverter.h"
#include "system.h"
#include "cores\DSPlayer\Filters\MadvrSettings.h"

enum MADVR_RENDER_LAYER
{
  RENDER_LAYER_ALL,
  RENDER_LAYER_UNDER,
  RENDER_LAYER_OVER
};

class IPaintCallbackMadvr
{
public:
  virtual ~IPaintCallbackMadvr() {};

  virtual bool IsCurrentThreadId() { return false; }
  virtual bool IsEnteringExclusive(){ return false; }
  virtual void EnableExclusive(bool bEnable){};
  virtual void SetMadvrPixelShader(){};
  virtual void GetProfileActiveName(std::string *profile){};
  virtual void SetResolution(){};
  virtual void RenderToTexture(MADVR_RENDER_LAYER layer){};
  virtual bool ParentWindowProc(HWND hWnd, UINT uMsg, WPARAM *wParam, LPARAM *lParam, LRESULT *ret) { return false; }
  virtual void SetMadvrPosition(CRect wndRect, CRect videoRect) {};
};

class ISettingCallbackMadvr
{
public:
  virtual ~ISettingCallbackMadvr() {};

  virtual void RestoreSettings(){};
  virtual void LoadSettings(MADVR_LOAD_TYPE type){};
  virtual void SetStr(CStdString path, CStdString sValue) {};
  virtual void SetBool(CStdString path, bool bValue) {};
  virtual void SetInt(CStdString path, int iValue) {};
  virtual void SetFloat(CStdString path, float fValue, int iConv = 100) {};
  virtual void SetDoubling(CStdString path, int iValue) {};
  virtual void SetDeintActive(CStdString path, int iValue) {};
  virtual void SetSmoothmotion(CStdString path, int iValue) {};
  virtual void SetDithering(CStdString path, int iValue) {};
};

class CMadvrCallback : public IPaintCallbackMadvr, public ISettingCallbackMadvr
{
public:
  /// Retrieve singleton instance
  static CMadvrCallback* Get();
  /// Destroy singleton instance
  static void Destroy()
  {
    delete m_pSingleton;
    m_pSingleton = NULL;
  }

  IPaintCallbackMadvr* GetCallback() { return m_pMadvr != NULL ? m_pMadvr : this; }
  ISettingCallbackMadvr* GetSetting() { return m_pSettingMadvr != NULL ? m_pSettingMadvr : this; }
  void SetCallback(IPaintCallbackMadvr* pMadvr) { m_pMadvr = pMadvr; }
  void SetSettingCallback(ISettingCallbackMadvr* pSettingMadvr) { m_pSettingMadvr = pSettingMadvr; }
  bool UsingMadvr();
  bool ReadyMadvr();
  bool IsEnteringExclusiveMadvr();
  bool IsInitMadvr() { return m_isInitMadvr; };
  void SetInitMadvr(bool b) { m_isInitMadvr = b; }
  bool GetRenderOnMadvr() { return m_renderOnMadvr; }
  void SetRenderOnMadvr(bool b) { m_renderOnMadvr = b; }
  void SetCurrentVideoLayer(MADVR_RENDER_LAYER layer) { m_currentVideoLayer = layer; }
  void IncRenderCount();
  void ResetRenderCount();
  bool GuiVisible(MADVR_RENDER_LAYER layer = RENDER_LAYER_ALL);
  

private:
  CMadvrCallback();
  ~CMadvrCallback();

  static CMadvrCallback* m_pSingleton;
  IPaintCallbackMadvr* m_pMadvr;
  ISettingCallbackMadvr* m_pSettingMadvr;
  bool m_isInitMadvr;
  bool m_renderOnMadvr;
  int m_renderUnderCount;
  int m_renderOverCount;
  MADVR_RENDER_LAYER m_currentVideoLayer;
};
