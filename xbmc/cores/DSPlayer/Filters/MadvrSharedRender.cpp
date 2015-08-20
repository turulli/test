/*
 *      Copyright (C) 2005-2014 Team XBMC
 *      http://xbmc.org
 *
 *      Copyright (C) 2014-2015 Aracnoz
 *      http://github.com/aracnoz/xbmc
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */
#include "MadvrSharedRender.h"
#include "mvrInterfaces.h"
#include "Utils/Log.h"
#include "guilib/GUIWindowManager.h"

const DWORD D3DFVF_VID_FRAME_VERTEX = D3DFVF_XYZRHW | D3DFVF_TEX1;

struct VID_FRAME_VERTEX
{
  float x;
  float y;
  float z;
  float rhw;
  float u;
  float v;
};

CMadvrSharedRender::CMadvrSharedRender()
{
  m_bUnderRender = false;
  m_bGuiVisible = false;
  m_bGuiVisibleOver = false;
}

CMadvrSharedRender::~CMadvrSharedRender()
{
  SAFE_RELEASE(m_pMadvrVertexBuffer);
  SAFE_RELEASE(m_pMadvrOverTexture);
  SAFE_RELEASE(m_pKodiOverSurface);
  SAFE_RELEASE(m_pKodiOverTexture);

  SAFE_RELEASE(m_pMadvrUnderTexture);
  SAFE_RELEASE(m_pKodiUnderSurface);
  SAFE_RELEASE(m_pKodiUnderTexture);

  SAFE_RELEASE(m_pKodiQueue);
  SAFE_RELEASE(m_pMadvrQueue);
  SAFE_RELEASE(m_pKodiProducer);
  SAFE_RELEASE(m_pKodiConsumer);
  SAFE_RELEASE(m_pMadvrProducer);
  SAFE_RELEASE(m_pMadvrConsumer);
}

HRESULT CMadvrSharedRender::CreateSharedResource(IDirect3DTexture9** ppTextureMadvr, IDirect3DTexture9** ppTextureKodi, IDirect3DSurface9** ppSurfaceKodi)
{
  HRESULT hr;
  HANDLE pSharedHandle = nullptr;

  if (FAILED(hr = m_pD3DDeviceMadVR->CreateTexture(m_dwTextureWidth, m_dwTextureHeight, 0, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, ppTextureMadvr, &pSharedHandle)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceKodi->CreateTexture(m_dwTextureWidth, m_dwTextureHeight, 0, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, ppTextureKodi, &pSharedHandle)))
    return hr;

  IDirect3DTexture9* pTexture = *ppTextureKodi;
  if (FAILED(hr = pTexture->GetSurfaceLevel(0, ppSurfaceKodi)))
    return hr;

  CMadvrCallback::Get()->Register(this);

  return hr;
}

HRESULT CMadvrSharedRender::CreateSharedQueueResource()
{
  HRESULT hr;

  // Initialize the surface queues
  SURFACE_QUEUE_DESC  desc;
  desc.Width = m_dwTextureWidth;
  desc.Height = m_dwTextureHeight;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.NumSurfaces = 1;
  desc.MetaDataSize = sizeof(UINT);
  desc.Flags = 0;

  if (FAILED(hr = CreateSurfaceQueue(&desc, m_pD3DDeviceMadVR, &m_pKodiQueue)))
    return hr;

  // Clone the queue
  SURFACE_QUEUE_CLONE_DESC cloneDesc;
  cloneDesc.MetaDataSize = 0;
  cloneDesc.Flags = 0;
  if (FAILED(hr = m_pKodiQueue->Clone(&cloneDesc, &m_pMadvrQueue)))
    return hr;

  // Open for m_pD3D9Queue
  if (FAILED(hr = m_pMadvrQueue->OpenProducer(m_pD3DDeviceMadVR, &m_pMadvrProducer)))
    return hr;
  if (FAILED(hr = m_pMadvrQueue->OpenConsumer(m_pD3DDeviceKodi, &m_pKodiConsumer)))
    return hr;

  // Open for m_pD3D11Queue
  if (FAILED(hr = m_pKodiQueue->OpenProducer(m_pD3DDeviceKodi, &m_pKodiProducer)))
    return hr;
  if (FAILED(hr = m_pKodiQueue->OpenConsumer(m_pD3DDeviceMadVR, &m_pMadvrConsumer)))
    return hr;

  return hr;
}

HRESULT CMadvrSharedRender::CreateTextures(IDirect3DDevice9Ex* pD3DDeviceKodi, IDirect3DDevice9Ex* pD3DDeviceMadVR, int width, int height)
{
  HRESULT hr;
  m_pD3DDeviceKodi = pD3DDeviceKodi;
  m_pD3DDeviceMadVR = pD3DDeviceMadVR;
  m_dwTextureWidth = width;
  m_dwTextureHeight = height;

  // Create vertex buffer
  if (FAILED(hr = m_pD3DDeviceMadVR->CreateVertexBuffer(sizeof(VID_FRAME_VERTEX) * 4, D3DUSAGE_WRITEONLY, D3DFVF_VID_FRAME_VERTEX, D3DPOOL_DEFAULT, &m_pMadvrVertexBuffer, NULL)))
    CLog::Log(LOGDEBUG, "%s Failed to create madVR vertex buffer", __FUNCTION__);

  // Create under surface queue
  if (FAILED(hr = CreateSharedQueueResource()))
    CLog::Log(LOGDEBUG, "%s Failed to create under surface queue", __FUNCTION__);

  // Create over shared texture
  if (FAILED(hr = CreateSharedResource(&m_pMadvrOverTexture, &m_pKodiOverTexture, &m_pKodiOverSurface)))
    CLog::Log(LOGDEBUG, "%s Failed to create over shared texture", __FUNCTION__);

  return hr;
}

HRESULT CMadvrSharedRender::Render(MADVR_RENDER_LAYER layer, int width, int height)
{
  if (!CMadvrCallback::Get()->GetRenderOnMadvr() || (g_graphicsContext.IsFullScreenVideo() && layer == RENDER_LAYER_UNDER))
    return CALLBACK_INFO_DISPLAY;

  m_dwWidth = width;
  m_dwHeight = height;

  // Dequeue GUI Textures
  DeQueue(layer);

  // Render the GUI on madVR
  RenderMadvr(layer);

  // Return to madVR if we rendered something
  return (m_bGuiVisible) ? CALLBACK_USER_INTERFACE : CALLBACK_INFO_DISPLAY;
}

void CMadvrSharedRender::DeQueue(MADVR_RENDER_LAYER layer)
{
  // Ensure to don't call the Dequeue twice with Clearbackground and RenderOSD callbacks
  if (layer == RENDER_LAYER_UNDER)
    m_bUnderRender = true;

  if (layer == RENDER_LAYER_OVER && m_bUnderRender)
  {
    m_bUnderRender = false;
    return;
  }

  CMetaData *metaData = DNew CMetaData();
  UINT size = sizeof(metaData);
  IDirect3DTexture9* pTexture9;

  HRESULT hr = m_pMadvrConsumer->Dequeue(__uuidof(IDirect3DTexture9), (void**)&pTexture9, &metaData, &size, 0);
  if (SUCCEEDED(hr))
  {
    SAFE_RELEASE(m_pMadvrUnderTexture);

    m_pMadvrUnderTexture = pTexture9;
    m_bGuiVisible = metaData->bGuiVisible;
    m_bGuiVisibleOver = metaData->bGuiVisibleOver;

    m_pMadvrProducer->Enqueue(pTexture9, NULL, NULL, NULL);
    m_pMadvrProducer->Flush(NULL, NULL);
  }
}

HRESULT CMadvrSharedRender::RenderMadvr(MADVR_RENDER_LAYER layer)
{
  HRESULT hr = E_UNEXPECTED;

  // If the over layer it's empty skip the rendering of the under layer and drawn everything over madVR
  if (layer == RENDER_LAYER_UNDER && !m_bGuiVisibleOver)
    return hr;

  if (layer == RENDER_LAYER_OVER)
    m_bGuiVisibleOver ? layer = RENDER_LAYER_OVER : layer = RENDER_LAYER_UNDER;

  // Store madVR States
  if (FAILED(hr = StoreMadDeviceState()))
    return hr;

  // Setup madVR Device
  if (FAILED(hr = SetupMadDeviceState()))
    return hr;

  // Setup Vertex Buffer
  if (FAILED(hr = SetupVertex()))
    return hr;

  // Draw Kodi shared texture on madVR D3D9 device
  if (FAILED(hr = RenderTexture(layer)))
    return hr;

  // Restore madVR states
  if (FAILED(hr = RestoreMadDeviceState()))
    return hr;

  return hr;
}

HRESULT CMadvrSharedRender::RenderToTexture(MADVR_RENDER_LAYER layer)
{
  HRESULT hr = S_OK;

  CMadvrCallback::Get()->SetCurrentVideoLayer(layer);

  if (layer == RENDER_LAYER_UNDER)
  {
    IDirect3DSurface9* pSurface = nullptr;
    IDirect3DTexture9* pTexture = nullptr;

    CMadvrCallback::Get()->ResetRenderCount();

    hr = m_pKodiConsumer->Dequeue(__uuidof(IDirect3DTexture9), (void**)&pTexture, NULL, NULL, 100);
    if (SUCCEEDED(hr))
    {
      pTexture->GetSurfaceLevel(0, &pSurface);
      m_pD3DDeviceKodi->SetRenderTarget(0, pSurface);
      m_pD3DDeviceKodi->Clear(0, NULL, D3DCLEAR_TARGET, D3DXCOLOR(0, 0, 0, 0), 1.0f, 0);

      m_pKodiUnderTexture = pTexture;
      m_pKodiUnderSurface = pSurface;
    }
  }
  else
  {
    m_pD3DDeviceKodi->SetRenderTarget(0, m_pKodiOverSurface);
    m_pD3DDeviceKodi->Clear(0, NULL, D3DCLEAR_TARGET, D3DXCOLOR(0, 0, 0, 0), 1.0f, 0);
  }

  return hr;
}

void CMadvrSharedRender::Flush()
{
  CMetaData *metaData = DNew CMetaData();
  metaData->bGuiVisible = CMadvrCallback::Get()->GuiVisible();
  metaData->bGuiVisibleOver = CMadvrCallback::Get()->GuiVisible(RENDER_LAYER_OVER);

  m_pKodiProducer->Enqueue(m_pKodiUnderTexture, &metaData, sizeof(metaData), NULL);
  m_pKodiProducer->Flush(NULL, NULL);
  SAFE_RELEASE(m_pKodiUnderSurface);
  SAFE_RELEASE(m_pKodiUnderTexture);
}


HRESULT CMadvrSharedRender::RenderTexture(MADVR_RENDER_LAYER layer)
{
  IDirect3DTexture9* pTexture;
  layer == RENDER_LAYER_UNDER ? pTexture = m_pMadvrUnderTexture : pTexture = m_pMadvrOverTexture;

  HRESULT hr = m_pD3DDeviceMadVR->SetStreamSource(0, m_pMadvrVertexBuffer, 0, sizeof(VID_FRAME_VERTEX));
  if (FAILED(hr))
    return hr;

  hr = m_pD3DDeviceMadVR->SetTexture(0, pTexture);
  if (FAILED(hr))
    return hr;

  hr = m_pD3DDeviceMadVR->DrawPrimitive(D3DPT_TRIANGLEFAN, 0, 2);
  if (FAILED(hr))
    return hr;

  return S_OK;
}

HRESULT CMadvrSharedRender::SetupVertex()
{
  VID_FRAME_VERTEX* vertices = nullptr;

  // Lock the vertex buffer
  HRESULT hr = m_pMadvrVertexBuffer->Lock(0, 0, (void**)&vertices, D3DLOCK_DISCARD);

  if (SUCCEEDED(hr))
  {
    RECT rDest;
    rDest.bottom = m_dwHeight;
    rDest.left = 0;
    rDest.right = m_dwWidth;
    rDest.top = 0;

    vertices[0].x = (float)rDest.left - 0.5f;
    vertices[0].y = (float)rDest.top - 0.5f;
    vertices[0].z = 0.0f;
    vertices[0].rhw = 1.0f;
    vertices[0].u = 0.0f;
    vertices[0].v = 0.0f;

    vertices[1].x = (float)rDest.right - 0.5f;
    vertices[1].y = (float)rDest.top - 0.5f;
    vertices[1].z = 0.0f;
    vertices[1].rhw = 1.0f;
    vertices[1].u = 1.0f;
    vertices[1].v = 0.0f;

    vertices[2].x = (float)rDest.right - 0.5f;
    vertices[2].y = (float)rDest.bottom - 0.5f;
    vertices[2].z = 0.0f;
    vertices[2].rhw = 1.0f;
    vertices[2].u = 1.0f;
    vertices[2].v = 1.0f;

    vertices[3].x = (float)rDest.left - 0.5f;
    vertices[3].y = (float)rDest.bottom - 0.5f;
    vertices[3].z = 0.0f;
    vertices[3].rhw = 1.0f;
    vertices[3].u = 0.0f;
    vertices[3].v = 1.0f;

    hr = m_pMadvrVertexBuffer->Unlock();
    if (FAILED(hr))
      return hr;
  }

  return hr;
}

HRESULT CMadvrSharedRender::StoreKodiDeviceState()
{
  HRESULT hr = E_UNEXPECTED;

  if (FAILED(hr = m_pD3DDeviceKodi->GetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, &m_KODI_D3DRS_SEPARATEALPHABLENDENABLE)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceKodi->GetRenderState(D3DRS_SRCBLENDALPHA, &m_KODI_D3DRS_SRCBLENDALPHA)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceKodi->GetRenderState(D3DRS_DESTBLENDALPHA, &m_KODI_D3DRS_DESTBLENDALPHA)))
    return hr;

  return hr;
}

HRESULT CMadvrSharedRender::StoreMadDeviceState()
{
  HRESULT hr = E_UNEXPECTED;

  if (FAILED(hr = m_pD3DDeviceMadVR->GetScissorRect(&m_oldScissorRect)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->GetVertexShader(&m_pOldVS)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->GetFVF(&m_dwOldFVF)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->GetTexture(0, &m_pOldTexture)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->GetStreamSource(0, &m_pOldStreamData, &m_nOldOffsetInBytes, &m_nOldStride)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->GetRenderState(D3DRS_CULLMODE, &m_D3DRS_CULLMODE)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->GetRenderState(D3DRS_LIGHTING, &m_D3DRS_LIGHTING)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->GetRenderState(D3DRS_ZENABLE, &m_D3DRS_ZENABLE)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->GetRenderState(D3DRS_ALPHABLENDENABLE, &m_D3DRS_ALPHABLENDENABLE)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->GetRenderState(D3DRS_SRCBLEND, &m_D3DRS_SRCBLEND)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->GetRenderState(D3DRS_DESTBLEND, &m_D3DRS_DESTBLEND)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->GetPixelShader(&m_pPix)))
    return hr;

  return hr;
}

HRESULT CMadvrSharedRender::SetupKodiDeviceState()
{
  HRESULT hr = E_UNEXPECTED;

  if (FAILED(hr = m_pD3DDeviceKodi->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceKodi->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceKodi->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA)))
    return hr;

  return hr;
}
HRESULT CMadvrSharedRender::SetupMadDeviceState()
{
  HRESULT hr = E_UNEXPECTED;

  RECT newScissorRect;
  newScissorRect.bottom = m_dwHeight;
  newScissorRect.top = 0;
  newScissorRect.left = 0;
  newScissorRect.right = m_dwWidth;

  if (FAILED(hr = m_pD3DDeviceMadVR->SetScissorRect(&newScissorRect)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->SetVertexShader(NULL)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->SetFVF(D3DFVF_VID_FRAME_VERTEX)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->SetPixelShader(NULL)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->SetRenderState(D3DRS_LIGHTING, FALSE)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->SetRenderState(D3DRS_ZENABLE, FALSE)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA)))
    return hr;

  return hr;
}

HRESULT CMadvrSharedRender::RestoreKodiDeviceState()
{
  HRESULT hr = E_UNEXPECTED;

  if (FAILED(hr = m_pD3DDeviceKodi->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, m_KODI_D3DRS_SEPARATEALPHABLENDENABLE)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceKodi->SetRenderState(D3DRS_SRCBLENDALPHA, m_KODI_D3DRS_SRCBLENDALPHA)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceKodi->SetRenderState(D3DRS_DESTBLENDALPHA, m_KODI_D3DRS_DESTBLENDALPHA)))
    return hr;

  return hr;
}

HRESULT CMadvrSharedRender::RestoreMadDeviceState()
{
  HRESULT hr = S_FALSE;

  if (FAILED(hr = m_pD3DDeviceMadVR->SetScissorRect(&m_oldScissorRect)))
    return hr;

  hr = m_pD3DDeviceMadVR->SetTexture(0, m_pOldTexture);

  if (m_pOldTexture)
    m_pOldTexture->Release();

  if (FAILED(hr))
    return hr;

  hr = m_pD3DDeviceMadVR->SetVertexShader(m_pOldVS);

  if (m_pOldVS)
    m_pOldVS->Release();

  if (FAILED(hr))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->SetFVF(m_dwOldFVF)))
    return hr;

  hr = m_pD3DDeviceMadVR->SetStreamSource(0, m_pOldStreamData, m_nOldOffsetInBytes, m_nOldStride);

  if (m_pOldStreamData)
    m_pOldStreamData->Release();

  if (FAILED(hr))
    return hr;

  hr = m_pD3DDeviceMadVR->SetPixelShader(m_pPix);
  if (m_pPix)
    m_pPix->Release();

  if (FAILED(hr))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->SetRenderState(D3DRS_CULLMODE, m_D3DRS_CULLMODE)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->SetRenderState(D3DRS_LIGHTING, m_D3DRS_LIGHTING)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->SetRenderState(D3DRS_ZENABLE, m_D3DRS_ZENABLE)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->SetRenderState(D3DRS_ALPHABLENDENABLE, m_D3DRS_ALPHABLENDENABLE)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->SetRenderState(D3DRS_SRCBLEND, m_D3DRS_SRCBLEND)))
    return hr;

  if (FAILED(hr = m_pD3DDeviceMadVR->SetRenderState(D3DRS_DESTBLEND, m_D3DRS_DESTBLEND)))
    return hr;

  return hr;
}