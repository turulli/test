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
#include "MadvrSharedRender.h"
#include "MadvrSettingsManager.h"

class CmadVRAllocatorPresenter
  : public ISubPicAllocatorPresenterImpl,
  public IMadvrAllocatorCallback,
  public ISubRenderCallback2,
  public IOsdRenderCallback
{
  Com::SmartPtr<IUnknown> m_pMVR;

public:

  CmadVRAllocatorPresenter(HWND hWnd, HRESULT& hr, CStdString &_Error);
  virtual ~CmadVRAllocatorPresenter();

  static void __stdcall ExclusiveCallback(LPVOID context, int event);

  DECLARE_IUNKNOWN
  STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

  // ISubRenderCallback
  STDMETHODIMP SetDevice(IDirect3DDevice9* pD3DDev) override;
  STDMETHODIMP Render(REFERENCE_TIME rtStart, int left, int top, int right, int bottom, int width, int height) override {
    return RenderEx(rtStart, 0, 0, left, top, right, bottom, width, height);
  };

  // ISubRenderCallback2
  STDMETHODIMP RenderEx(REFERENCE_TIME rtStart, REFERENCE_TIME rtStop, REFERENCE_TIME atpf,
    int left, int top, int right, int bottom, int width, int height) override;

  // ISubPicAllocatorPresenter
  STDMETHODIMP CreateRenderer(IUnknown** ppRenderer) override;
  STDMETHODIMP_(void) SetPosition(RECT w, RECT v) override;
  STDMETHODIMP_(SIZE) GetVideoSize(bool fCorrectAR = true) override;
  STDMETHODIMP_(bool) Paint(bool bAll) override;
  STDMETHODIMP GetDIB(BYTE* lpDib, DWORD* size) override;
  STDMETHODIMP SetPixelShader(LPCSTR pSrcData, LPCSTR pTarget) override;

  // IOsdRenderCallback
  STDMETHODIMP ClearBackground(LPCSTR name, REFERENCE_TIME frameStart, RECT *fullOutputRect, RECT *activeVideoRect);
  STDMETHODIMP RenderOsd(LPCSTR name, REFERENCE_TIME frameStart, RECT *fullOutputRect, RECT *activeVideoRect);

  // IMadvrAllocatorCallback
  virtual bool IsEnteringExclusive(){ return m_isEnteringExclusive; }
  virtual void EnableExclusive(bool bEnable);
  virtual void SetMadvrPixelShader();
  virtual void SetResolution();
  virtual bool ParentWindowProc(HWND hWnd, UINT uMsg, WPARAM *wParam, LPARAM *lParam, LRESULT *ret);
  virtual void SetMadvrPosition(CRect wndRect, CRect videoRect);

private:
  void ConfigureMadvr();
  EXCLUSIVEMODECALLBACK m_exclusiveCallback;
  bool m_bIsFullscreen;
  bool m_firstBoot;
  bool m_isEnteringExclusive;
  int m_shaderStage;
  int m_kodiGuiDirtyAlgo;
  bool m_updateDisplayLatencyForMadvr;
  CMadvrSharedRender *m_pMadvrShared;
  CMadvrSettingsManager *m_pSettingsManager;
};

