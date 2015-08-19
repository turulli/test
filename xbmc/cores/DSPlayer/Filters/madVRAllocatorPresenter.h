/*
 * $Id$
 *
 * (C) 2006-2011 see AUTHORS
 *
 * This file is part of mplayerc.
 *
 * Mplayerc is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Mplayerc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include "AllocatorCommon.h"
#include "mvrInterfaces.h"
#include "MadvrCallback.h"
#include "threads/Thread.h"
#include "MadvrSharedRender.h"
#include "MadvrSettingsManager.h"
#include "utils/log.h"

class CmadVRAllocatorPresenter
  : public ISubPicAllocatorPresenterImpl,
  public IPaintCallbackMadvr
{
  class COsdRenderCallback : public CUnknown, public IOsdRenderCallback, public CCritSec
  {
    CmadVRAllocatorPresenter* m_pDXRAP;

  public:
    COsdRenderCallback(CmadVRAllocatorPresenter* pDXRAP)
      : CUnknown(_T("COsdRender"), NULL)
      , m_pDXRAP(pDXRAP) {
    }

    DECLARE_IUNKNOWN
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv) {
      return
        QI(IOsdRenderCallback)
        __super::NonDelegatingQueryInterface(riid, ppv);
    }

    void SetDXRAP(CmadVRAllocatorPresenter* pDXRAP) {
      CAutoLock cAutoLock(this);
      m_pDXRAP = pDXRAP;
    }

    // IOsdRenderCallback

    STDMETHODIMP ClearBackground(LPCSTR name, REFERENCE_TIME frameStart, RECT *fullOutputRect, RECT *activeVideoRect){
      CAutoLock cAutoLock(this);
      return m_pDXRAP ? m_pDXRAP->ClearBackground(name, frameStart, fullOutputRect, activeVideoRect) : E_UNEXPECTED;
    }
    STDMETHODIMP RenderOsd(LPCSTR name, REFERENCE_TIME frameStart, RECT *fullOutputRect, RECT *activeVideoRect){
      CAutoLock cAutoLock(this);
      return m_pDXRAP ? m_pDXRAP->RenderOsd(name, frameStart, fullOutputRect, activeVideoRect) : E_UNEXPECTED;
    }
    STDMETHODIMP SetDevice(IDirect3DDevice9* pD3DDev) {
      CAutoLock cAutoLock(this);
      return m_pDXRAP ? m_pDXRAP->SetDeviceOsd(pD3DDev) : E_UNEXPECTED;
    }
  };

  class CSubRenderCallback : public CUnknown, public ISubRenderCallback2, public CCritSec
  {
    CmadVRAllocatorPresenter* m_pDXRAP;

  public:
    CSubRenderCallback(CmadVRAllocatorPresenter* pDXRAP)
      : CUnknown(_T("CSubRender"), NULL)
      , m_pDXRAP(pDXRAP) {
    }

    DECLARE_IUNKNOWN
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv) {
      return
        QI(ISubRenderCallback)
        QI(ISubRenderCallback2)
        __super::NonDelegatingQueryInterface(riid, ppv);
    }

    void SetDXRAP(CmadVRAllocatorPresenter* pDXRAP) {
      CAutoLock cAutoLock(this);
      m_pDXRAP = pDXRAP;
    }

    // ISubRenderCallback

    STDMETHODIMP SetDevice(IDirect3DDevice9* pD3DDev) {
      CAutoLock cAutoLock(this);
      return m_pDXRAP ? m_pDXRAP->SetDevice(pD3DDev) : E_UNEXPECTED;
    }

    STDMETHODIMP Render(REFERENCE_TIME rtStart, int left, int top, int right, int bottom, int width, int height) {
      CAutoLock cAutoLock(this);
      return m_pDXRAP ? m_pDXRAP->Render(rtStart, 0, 0, left, top, right, bottom, width, height) : E_UNEXPECTED;
    }

    // ISubRendererCallback2

    STDMETHODIMP RenderEx(REFERENCE_TIME rtStart, REFERENCE_TIME rtStop, REFERENCE_TIME AvgTimePerFrame, int left, int top, int right, int bottom, int width, int height) {
      CAutoLock cAutoLock(this);
      return m_pDXRAP ? m_pDXRAP->Render(rtStart, rtStop, AvgTimePerFrame, left, top, right, bottom, width, height) : E_UNEXPECTED;
    }
  };

public:

  CmadVRAllocatorPresenter(HWND hWnd, HRESULT& hr, CStdString &_Error);
  virtual ~CmadVRAllocatorPresenter();

  static void __stdcall ExclusiveCallback(LPVOID context, int event);

  DECLARE_IUNKNOWN
  STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

  // IOsdRenderCallback
  STDMETHODIMP ClearBackground(LPCSTR name, REFERENCE_TIME frameStart, RECT *fullOutputRect, RECT *activeVideoRect);
  STDMETHODIMP RenderOsd(LPCSTR name, REFERENCE_TIME frameStart, RECT *fullOutputRect, RECT *activeVideoRect);
  STDMETHODIMP SetDeviceOsd(IDirect3DDevice9* pD3DDev);

  // ISubPicAllocatorPresenter
  HRESULT SetDevice(IDirect3DDevice9* pD3DDev);
  HRESULT Render(REFERENCE_TIME rtStart, REFERENCE_TIME rtStop, REFERENCE_TIME atpf, int left, int top, int bottom, int right, int width, int height);
  STDMETHODIMP CreateRenderer(IUnknown** ppRenderer);
  STDMETHODIMP_(void) SetPosition(RECT w, RECT v);
  STDMETHODIMP_(SIZE) GetVideoSize(bool fCorrectAR);
  STDMETHODIMP_(bool) Paint(bool fAll);
  STDMETHODIMP GetDIB(BYTE* lpDib, DWORD* size);
  STDMETHODIMP SetPixelShader(LPCSTR pSrcData, LPCSTR pTarget);

  //IPaintCallbackMadvr
  virtual bool IsCurrentThreadId();
  virtual bool IsEnteringExclusive(){ return m_isEnteringExclusive; }
  virtual void EnableExclusive(bool bEnable);
  virtual void SetMadvrPixelShader();
  virtual void RestoreMadvrSettings();
  virtual void SetResolution();
  virtual void RenderToTexture(MADVR_RENDER_LAYER layer){ m_pMadvrShared->RenderToTexture(layer); };
  virtual bool ParentWindowProc(HWND hWnd, UINT uMsg, WPARAM *wParam, LPARAM *lParam, LRESULT *ret);
  virtual void SetMadvrPosition(CRect wndRect, CRect videoRect);

  //Madvr Settings
  virtual void SettingSetStr(CStdString path, CStdString sValue) { m_pSettingsManager->SetStr(path, sValue); };
  virtual void SettingSetBool(CStdString path, BOOL bValue) { m_pSettingsManager->SetBool(path, bValue); };
  virtual void SettingSetInt(CStdString path, int iValue) { m_pSettingsManager->SetInt(path, iValue); };
  virtual void SettingSetDoubling(CStdString path, int iValue) { m_pSettingsManager->SetDoubling(path, iValue); };
  virtual void SettingSetDeintActive(CStdString path, int iValue) { m_pSettingsManager->SetDeintActive(path, iValue); };
  virtual void SettingSetSmoothmotion(CStdString path, int iValue) { m_pSettingsManager->SetSmoothmotion(path, iValue); };
  virtual void SettingSetDithering(CStdString path, int iValue) { m_pSettingsManager->SetDithering(path, iValue); };

private:
  void ConfigureMadvr();
  Com::SmartPtr<IUnknown> m_pDXR;
  Com::SmartPtr<IOsdRenderCallback> m_pORCB;
  Com::SmartPtr<ISubRenderCallback2> m_pSRCB;
  Com::SmartSize m_ScreenSize;
  EXCLUSIVEMODECALLBACK m_exclusiveCallback;
  static ThreadIdentifier m_threadID;
  bool m_bIsFullscreen;
  bool m_firstBoot;
  bool m_isEnteringExclusive;
  int m_shaderStage;
  int m_kodiGuiDirtyAlgo;
  bool m_updateDisplayLatencyForMadvr;
  CMadvrSharedRender *m_pMadvrShared;
  CMadvrSettingsManager *m_pSettingsManager;
};

