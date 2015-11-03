/*
 *      Copyright (C) 2005-2009 Team XBMC
 *      http://www.xbmc.org
 *
 *		Copyright (C) 2010-2013 Eduard Kytmanov
 *		http://www.avmedia.su
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

#include "DSPlayer.h"
#include "DSUtil/DSUtil.h" // unload loaded filters
#include "DSUtil/SmartPtr.h"
#include "Filters/RendererSettings.h"

#include "windowing/windows/winsystemwin32.h" //Important needed to get the right hwnd
#include "xbmc/GUIInfoManager.h"
#include "utils/SystemInfo.h"
#include "input/MouseStat.h"
#include "settings/Settings.h"
#include "FileItem.h"
#include "utils/log.h"
#include "URL.h"

#include "guilib/GUIWindowManager.h"
#include "dialogs/GUIDialogBusy.h"
#include "windowing/WindowingFactory.h"
#include "dialogs/GUIDialogOK.h"
#include "PixelShaderList.h"
#include "guilib/LocalizeStrings.h"
#include "dialogs/GUIDialogSelect.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "video/windows/GUIWindowVideoBase.h"
#include "cores/AudioEngine/AEFactory.h"
#include "messaging/ApplicationMessenger.h"
#include "DSInputStreamPVRManager.h"
#include "pvr/PVRManager.h"
#include "pvr/windows/GUIWindowPVRBase.h"
#include "pvr/channels/PVRChannel.h"
#include "settings/AdvancedSettings.h"
#include "Application.h"
#include "GUIUserMessages.h"
#include "input/Key.h"
#include "settings/MediaSettings.h"
#include "settings/MediaSourceSettings.h"
#include "cores/DSPlayer/dsgraph.h"
#include "settings/MediaSettings.h"
#include "settings/DisplaySettings.h"
#include "MadvrCallback.h"
#include "DSPlayerDatabase.h"

using namespace PVR;
using namespace std;
using namespace KODI::MESSAGING;

DSPLAYER_STATE CDSPlayer::PlayerState = DSPLAYER_CLOSED;
CFileItem CDSPlayer::currentFileItem;
CGUIDialogBoxBase *CDSPlayer::errorWindow = NULL;
ThreadIdentifier CDSPlayer::m_threadID = 0;
ThreadIdentifier CDSGraphThread::m_threadID = 0;
HWND CDSPlayer::m_hWnd = 0;

CDSPlayer::CDSPlayer(IPlayerCallback& callback)
  : IPlayer(callback),
  CThread("CDSPlayer thread"),
  m_hReadyEvent(true),
  m_pGraphThread(this),
  m_pDSGraphThread(this),
  m_bEof(false),
  m_renderManager(m_pClock)
{
  m_HasVideo = false;
  m_HasAudio = false;
  m_isMadvr = (CSettings::GetInstance().GetString(CSettings::SETTING_DSPLAYER_VIDEORENDERER) == "madVR");
  if (m_isMadvr)
  { 
    if (InitMadvrWindow(m_hWnd))
      CLog::Log(LOGDEBUG, "%s : Create DSPlayer window for madVR - hWnd: %i", __FUNCTION__, m_hWnd);
  }

  /* Suspend AE temporarily so exclusive or hog-mode sinks */
  /* don't block DSPlayer access to audio device  */
  if (!CAEFactory::Suspend())
  {
    CLog::Log(LOGNOTICE, __FUNCTION__, "Failed to suspend AudioEngine before launching DSPlayer");
  }

  CoInitializeEx(NULL, COINIT_MULTITHREADED);

  m_pClock.GetClock(); // Reset the clock
  g_dsGraph = new CDSGraph(&m_pClock, callback, m_renderManager);

  // Change DVD Clock, time base
  CDVDClock::SetTimeBase((int64_t)DS_TIME_BASE);
}

CDSPlayer::~CDSPlayer()
{
  /* Resume AE processing of XBMC native audio */
  if (!CAEFactory::Resume())
  {
    CLog::Log(LOGFATAL, __FUNCTION__, "Failed to restart AudioEngine after return from DSPlayer");
  }

  if (PlayerState != DSPLAYER_CLOSED)
    CloseFile();

  UnloadExternalObjects();
  CLog::Log(LOGDEBUG, "%s External objects unloaded", __FUNCTION__);

  // Restore DVD Player time base clock
  CDVDClock::SetTimeBase(DVD_TIME_BASE);

  // Save Shader settings
  g_dsSettings.pixelShaderList->SaveXML();

  CoUninitialize();

  if (m_isMadvr)
   DeInitMadvrWindow();

  SAFE_DELETE(g_dsGraph);
  SAFE_DELETE(g_pPVRStream);

  CLog::Log(LOGNOTICE, "%s DSPlayer is now closed", __FUNCTION__);
}

int CDSPlayer::GetSubtitleCount()
{
  if (CGraphFilters::Get()->HasSubFilter())
  {
    return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetSubfilterCount() : 0;
  }
  else {
    return (CStreamsManager::Get()) ? CStreamsManager::Get()->SubtitleManager->GetSubtitleCount() : 0;
  }
}

int CDSPlayer::GetSubtitle()
{
  if (CGraphFilters::Get()->HasSubFilter())
  {
    return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetSubfilter() : 0;
  }
  else {
    return (CStreamsManager::Get()) ? CStreamsManager::Get()->SubtitleManager->GetSubtitle() : 0;
  }
}

void CDSPlayer::SetSubtitle(int iStream)
{
  if (CGraphFilters::Get()->HasSubFilter())
  {
    if (CStreamsManager::Get()) CStreamsManager::Get()->SetSubfilter(iStream);
  }
  else {
    if (CStreamsManager::Get()) CStreamsManager::Get()->SubtitleManager->SetSubtitle(iStream);
  }
}

bool CDSPlayer::GetSubtitleVisible()
{
  if (CGraphFilters::Get()->HasSubFilter())
  {
    return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetSubfilterVisible() : true;
  }
  else {
    return (CStreamsManager::Get()) ? CStreamsManager::Get()->SubtitleManager->GetSubtitleVisible() : true;
  }
}

void CDSPlayer::SetSubtitleVisible(bool bVisible)
{
  if (CGraphFilters::Get()->HasSubFilter())
  {
    if (CStreamsManager::Get())
      CStreamsManager::Get()->SetSubfilterVisible(bVisible);
  }
  else {
    if (CStreamsManager::Get())
      CStreamsManager::Get()->SubtitleManager->SetSubtitleVisible(bVisible);
  }
}

void CDSPlayer::AddSubtitle(const std::string& strSubPath) {

  if (CGraphFilters::Get()->HasSubFilter())
  {
    if (CStreamsManager::Get())
      CStreamsManager::Get()->SetSubfilter(CStreamsManager::Get()->AddSubtitle(strSubPath));
  }
  else {
    if (CStreamsManager::Get())
      CStreamsManager::Get()->SubtitleManager->SetSubtitle(CStreamsManager::Get()->SubtitleManager->AddSubtitle(strSubPath));
  }
}

void CDSPlayer::ShowEditionDlg(bool playStart)
{
  UINT count = GetEditionsCount();

  if (count < 2)
    return;

  if (playStart && m_PlayerOptions.starttime > 0)
  {
    CDSPlayerDatabase db;
    if (db.Open())
    {
      CEdition edition;
      if (db.GetResumeEdition(currentFileItem.GetPath(), edition))
      {
        CLog::Log(LOGDEBUG, "%s select bookmark, edition with idx %i selected", __FUNCTION__, edition.editionNumber);
        SetEdition(edition.editionNumber);
        return;
      }
    }
  }

  // Start madVR render
  if (CMadvrCallback::Get()->UsingMadvr())
  {
    PostMessage(new CDSMsgBool(CDSMsg::PLAYER_PLAY, true), false);
    PostMessage(new CDSMsgBool(CDSMsg::PLAYER_PAUSE, true), false);
  }

  CGUIDialogSelect *dialog = (CGUIDialogSelect *)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);

  bool listAllTitles = false;
  UINT minLength;// = CSettings::GetInstance().GetInt(CSettings::SETTING_DSPLAYER_MINTITLELENGTH);

  while (true)
  {
    std::vector<UINT> editionOptions;

    dialog->SetHeading(IsMatroskaEditions() ? 55025 : 55026);

    CLog::Log(LOGDEBUG, "%s Edition count - %i", __FUNCTION__, count);
    int selected = GetEdition();
    for (UINT i = 0; i < count; i++)
    {
      std::string name;
      REFERENCE_TIME duration;

      GetEditionInfo(i, name, &duration);

      if (duration == _I64_MIN || listAllTitles || count == 1 || duration >= DS_TIME_BASE * 60 * minLength)
      {
        if (i == selected)
          selected = editionOptions.size();

        if (name.length() == 0)
          name = "Unnamed";
        dialog->Add(name);
        editionOptions.push_back(i);
      }
    }

    if (count > 1 && count != editionOptions.size())
    {
      dialog->Add(g_localizeStrings.Get(55027));
    }

    dialog->SetSelected(selected);
    dialog->Open();

    selected = dialog->GetSelectedLabel();
    if (selected >= 0)
    {
      if (selected == editionOptions.size())
      {
        listAllTitles = true;
        continue;
      }
      UINT idx = editionOptions[selected];
      CLog::Log(LOGDEBUG, "%s edition with idx %i selected", __FUNCTION__, idx);
      SetEdition(idx);
      break;
    }
    break;
  }
}

bool CDSPlayer::WaitForFileClose()
{
  if (!WaitForThreadExit(100) || !m_pGraphThread.WaitForThreadExit(100) || !m_pDSGraphThread.WaitForThreadExit(100))
    return false;

  return true;
}

bool CDSPlayer::OpenFileInternal(const CFileItem& file)
{
  try
  {
    CLog::Log(LOGNOTICE, "%s - DSPlayer: Opening: %s", __FUNCTION__, file.GetPath().c_str());
    if (PlayerState != DSPLAYER_CLOSED)
      CloseFile();

    if(!WaitForFileClose())
      return false;

    PlayerState = DSPLAYER_LOADING;
    currentFileItem = file;

    m_hReadyEvent.Reset();

    m_renderManager.PreInit(RENDERER_DSHOW);

    Create();

    // wait for the ready event
    CGUIDialogBusy::WaitOnEvent(m_hReadyEvent, g_advancedSettings.m_videoBusyDialogDelay_ms, false);

    if (PlayerState != DSPLAYER_ERROR)
    {
      float fValue;

      // Select Audio Stream
      int iLibrary = CMediaSettings::GetInstance().GetCurrentVideoSettings().m_AudioStream;

      if ((iLibrary < GetAudioStreamCount()) && !(iLibrary < 0))
        g_application.m_pPlayer->SetAudioStream(iLibrary);

      // Select Subtitle Stream and set Delay
      fValue = CMediaSettings::GetInstance().GetCurrentVideoSettings().m_SubtitleDelay;
      if (CGraphFilters::Get()->HasSubFilter())
      {
        if (CStreamsManager::Get()) CStreamsManager::Get()->SelectBestSubtitle();
        if (CStreamsManager::Get()) CStreamsManager::Get()->SetSubTitleDelay(fValue);
      }
      else
      {
        if (CStreamsManager::Get()) CStreamsManager::Get()->SubtitleManager->SelectBestSubtitle();
        if (CStreamsManager::Get()) CStreamsManager::Get()->SubtitleManager->SetSubtitleDelay(fValue);
      }

      // Get Audio Interface LAV AUDIO/FFDSHOW to setting Delay
      if (CStreamsManager::Get())
      {
        if (CStreamsManager::Get()->SetAudioInterface())
        {
          fValue = CMediaSettings::GetInstance().GetCurrentVideoSettings().m_AudioDelay;
          CStreamsManager::Get()->SetAVDelay(fValue);
        }
      }

      CMediaSettings::GetInstance().GetAtStartVideoSettings() = CMediaSettings::GetInstance().GetCurrentVideoSettings();

      m_HasVideo = true;
      m_HasAudio = true;

      // Madvr Settings
      if (CMadvrCallback::Get()->UsingMadvr())
        SetMadvrResolution();

      /*
      if (CSettings::GetInstance().GetBool(CSettings::SETTING_DSPLAYER_SHOWBDTITLECHOICE))
        ShowEditionDlg(true);
        */

      // Seek
      if (m_PlayerOptions.starttime > 0)
        PostMessage(new CDSMsgPlayerSeekTime(SEC_TO_DS_TIME(m_PlayerOptions.starttime), 1U, false), false);
      else
        PostMessage(new CDSMsgPlayerSeekTime(0, 1U, false), false);

      // Starts playback
      PostMessage(new CDSMsgBool(CDSMsg::PLAYER_PLAY, true), false);

      if (CGraphFilters::Get()->IsDVD())
        CStreamsManager::Get()->LoadDVDStreams();
    }

    return (PlayerState != DSPLAYER_ERROR);
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s - Exception thrown on open", __FUNCTION__);
    return false;
  }
}

bool CDSPlayer::OpenFile(const CFileItem& file, const CPlayerOptions &options)
{
  if (m_isMadvr && !CGraphFilters::Get()->IsRegisteredFilter(MADVR_FILTERSTR))
  {
    CLog::Log(LOGDEBUG, "%s - madVR it's not installed on the system pls download it before to use it with DSPlayer", __FUNCTION__);

    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error, g_localizeStrings.Get(90023), g_localizeStrings.Get(90024), 6000L, false);
    return false;
  }

  if (!CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOSCREEN_FAKEFULLSCREEN))
  {
    g_Windowing.SetWindowedForMadvr();
    CGraphFilters::Get()->SetKodiRealFS(true);
    CSettings::GetInstance().SetBool(CSettings::SETTING_VIDEOSCREEN_FAKEFULLSCREEN, true);
  }

  CLog::Log(LOGNOTICE, "%s - DSPlayer: Opening: %s", __FUNCTION__, file.GetPath().c_str());

  CFileItem fileItem = file;
  m_PlayerOptions = options;

  if (fileItem.IsInternetStream())
  {
    CURL url(fileItem.GetPath());
    url.SetProtocolOptions("");
    fileItem.SetPath(url.Get());
  }
  else if (fileItem.IsPVR())
  {
    g_pPVRStream = new CDSInputStreamPVRManager(this);
    return g_pPVRStream->Open(fileItem);
  }

  return OpenFileInternal(fileItem);
}

bool CDSPlayer::CloseFile(bool reopen)
{
  CSingleLock lock(m_CleanSection);

  // reset intial delay in decoder interface
  if (CStreamsManager::Get()) CStreamsManager::Get()->resetDelayInterface();

  if (PlayerState == DSPLAYER_CLOSED || PlayerState == DSPLAYER_CLOSING)
    return true;

  PlayerState = DSPLAYER_CLOSING;
  m_HasVideo = false;
  m_HasAudio = false;

  // set the abort request so that other threads can finish up
  m_bEof = g_dsGraph->IsEof();

  g_dsGraph->CloseFile();

  PlayerState = DSPLAYER_CLOSED;

  // Stop threads
  m_pDSGraphThread.StopThread(false);
  m_pGraphThread.StopThread(false);
  StopThread(false);

  m_renderManager.UnInit();

  CLog::Log(LOGDEBUG, "%s File closed", __FUNCTION__);
  return true;
}

void CDSPlayer::GetVideoStreamInfo(SPlayerVideoStreamInfo &info)
{
  CSingleLock lock(m_StateSection);
  info.width = (CStreamsManager::Get()) ? CStreamsManager::Get()->GetPictureWidth() : 0;
  info.height = (CStreamsManager::Get()) ? CStreamsManager::Get()->GetPictureHeight() : 0;
  info.videoCodecName = (CStreamsManager::Get()) ? CStreamsManager::Get()->GetVideoCodecName() : "";
  info.videoAspectRatio = (float)info.width / (float)info.height;
  CRect viewRect;
  m_renderManager.GetVideoRect(info.SrcRect, info.DestRect, viewRect);
  info.stereoMode == "";
}


void CDSPlayer::GetAudioStreamInfo(int index, SPlayerAudioStreamInfo &info)
{
  if (index == CURRENT_STREAM)
    index = GetAudioStream();

  CSingleLock lock(m_StateSection);

  CStdString strStreamName;
  CStdString label;
  CStdString codecname;

  info.bitrate = (CStreamsManager::Get()) ? CStreamsManager::Get()->GetBitsPerSample() : 0;
  info.audioCodecName = (CStreamsManager::Get()) ? CStreamsManager::Get()->GetAudioCodecName() : "";
  if (CStreamsManager::Get()) CStreamsManager::Get()->GetAudioStreamName(index, strStreamName);
  info.language = strStreamName;
  info.channels = (CStreamsManager::Get()) ? CStreamsManager::Get()->GetChannels(index) : 0;
  info.samplerate = (CStreamsManager::Get()) ? CStreamsManager::Get()->GetSampleRate(index) : 0;
  codecname = (CStreamsManager::Get()) ? CStreamsManager::Get()->GetAudioCodecDisplayName(index) : "";
  StringUtils::ToUpper(codecname);

  if (!CSettings::GetInstance().GetBool(CSettings::SETTING_DSPLAYER_SHOWSPLITTERDETAIL) ||
      CGraphFilters::Get()->UsingMediaPortalTsReader())
  { 
    label.Format("%s - (%s, %d Hz, %i Channels)", strStreamName, codecname, info.samplerate, info.channels);
    info.name = label;
  }
  else
    info.name = strStreamName;
}


void CDSPlayer::GetSubtitleStreamInfo(int index, SPlayerSubtitleStreamInfo &info)
{
  CStdString strStreamName;
  if (CGraphFilters::Get()->HasSubFilter())
  {
    if (CStreamsManager::Get()) CStreamsManager::Get()->GetSubfilterName(index, strStreamName);
  }
  else {
    if (CStreamsManager::Get()) CStreamsManager::Get()->SubtitleManager->GetSubtitleName(index, strStreamName);
  }
  info.language = strStreamName;
  info.name = strStreamName;
}

bool CDSPlayer::IsPlaying() const
{
  return !m_bStop;
}

bool CDSPlayer::HasVideo() const
{
  return m_HasVideo;
}
bool CDSPlayer::HasAudio() const
{
  return m_HasAudio;
}

void CDSPlayer::GetAudioInfo(std::string& strAudioInfo)
{
  CSingleLock lock(m_StateSection);
  strAudioInfo = g_dsGraph->GetAudioInfo();
}

void CDSPlayer::GetVideoInfo(std::string& strVideoInfo)
{
  CSingleLock lock(m_StateSection);
  strVideoInfo = g_dsGraph->GetVideoInfo();
}

void CDSPlayer::GetGeneralInfo(std::string& strGeneralInfo)
{
  CSingleLock lock(m_StateSection);
  strGeneralInfo = g_dsGraph->GetGeneralInfo();
}

float CDSPlayer::GetAVDelay()
{
  float fValue = 0.0f;

  if (CStreamsManager::Get())
    fValue = CStreamsManager::Get()->GetAVDelay();

  return fValue;
}

void CDSPlayer::SetAVDelay(float fValue)
{
  //get displaylatency
  int iDisplayLatency = m_renderManager.GetDisplayLatency() * 1000;

  if (CStreamsManager::Get()) CStreamsManager::Get()->SetAVDelay(fValue,iDisplayLatency);
}

float CDSPlayer::GetSubTitleDelay()
{
  float fValue = 0.0f;

  if (CStreamsManager::Get())
  {
    if (CGraphFilters::Get()->HasSubFilter())
      fValue = CStreamsManager::Get()->GetSubTitleDelay();
    else
      fValue = CStreamsManager::Get()->SubtitleManager->GetSubtitleDelay();
  }
  return fValue;
}

void CDSPlayer::SetSubTitleDelay(float fValue)
{
  if (CStreamsManager::Get())
  {
    if (CGraphFilters::Get()->HasSubFilter())
      CStreamsManager::Get()->SetSubTitleDelay(fValue);
    else
      CStreamsManager::Get()->SubtitleManager->SetSubtitleDelay(fValue);
  }
}

//CThread
void CDSPlayer::OnStartup()
{
  m_threadID = CThread::GetCurrentThreadId();
}

void CDSPlayer::OnExit()
{
  if (PlayerState == DSPLAYER_LOADING)
    PlayerState = DSPLAYER_ERROR;

  // In case of, set the ready event
  // Prevent a dead loop
  m_hReadyEvent.Set();
  m_bStop = true;
  m_threadID = 0;
  if (m_PlayerOptions.identify == false)
  {
    if (!m_bEof || PlayerState == DSPLAYER_ERROR)
      m_callback.OnPlayBackStopped();
    else
      m_callback.OnPlayBackEnded();
  }

  m_PlayerOptions.identify = false;
}

void CDSPlayer::Process()
{
  m_hDSGraph = E_FAIL;
  CLog::Log(LOGNOTICE, "%s - Creating DS Graph", __FUNCTION__);

  // Create DS Graph thread
  m_hDSGraphEvent.Reset();
  m_pDSGraphThread.Create();

  // Wait for DS Graph creation;
  m_hDSGraphEvent.Wait();

  if (FAILED(m_hDSGraph))
  {
    CLog::Log(LOGERROR, "%s - Failed creating DS Graph", __FUNCTION__);
    PlayerState = DSPLAYER_ERROR;
    return;
  }

  m_pGraphThread.SetCurrentRate(1);
  m_pGraphThread.Create();

  CLog::Log(LOGNOTICE, "%s - Successfully creating DS Graph", __FUNCTION__);

  UpdateApplication();

  g_dsSettings.pRendererSettings->bAllowFullscreen = m_PlayerOptions.fullscreen;

  if (m_PlayerOptions.identify == false) m_callback.OnPlayBackStarted();

  // Start playback
  // If there's an error, the lock must be released in order to show the error dialog
  m_hReadyEvent.Set();

  while (!m_bStop && PlayerState != DSPLAYER_CLOSED && PlayerState != DSPLAYER_LOADING)
    HandleMessages();
}

void CDSPlayer::DeInitMadvrWindow()
{
  // remove ourself as user data to ensure we're not called anymore
  SetWindowLongPtr(m_hWnd, GWL_USERDATA, 0);

  // destroy the hidden window
  DestroyWindow(m_hWnd);

  // unregister the window class
  UnregisterClass(m_className.c_str(), m_hInstance);

  // reset the hWnd
  m_hWnd = NULL;
}

bool CDSPlayer::InitMadvrWindow(HWND &hWnd)
{
  m_hInstance = (HINSTANCE)GetModuleHandle(NULL);
  if (m_hInstance == NULL)
    CLog::Log(LOGDEBUG, "%s : GetModuleHandle failed with %d", __FUNCTION__, GetLastError());

  RESOLUTION_INFO res = CDisplaySettings::GetInstance().GetCurrentResolutionInfo();
  int nWidth = res.iWidth;
  int nHeight = res.iHeight;
  m_className = "Kodi:DSPlayer";

  // Register the windows class
  WNDCLASS wndClass;

  wndClass.style = CS_HREDRAW | CS_VREDRAW | CS_NOCLOSE;
  wndClass.lpfnWndProc = CDSPlayer::WndProc;
  wndClass.cbClsExtra = 0;
  wndClass.cbWndExtra = 0;
  wndClass.hInstance = m_hInstance;
  wndClass.hIcon = NULL;
  wndClass.hCursor = NULL;
  wndClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  wndClass.lpszMenuName = NULL;
  wndClass.lpszClassName = m_className.c_str();

  if (!RegisterClass(&wndClass))
  {
    CLog::Log(LOGERROR, "%s : RegisterClass failed with %d", __FUNCTION__, GetLastError());
    return false;
  }

  hWnd = CreateWindow(m_className.c_str(), m_className.c_str(),
    WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
    0, 0, nWidth, nHeight, 
    g_hWnd, NULL, m_hInstance, NULL);
  if (hWnd == NULL)
  {
    CLog::Log(LOGERROR, "%s : CreateWindow failed with %d", __FUNCTION__, GetLastError());
    return false;
  }

  if (hWnd)
    SetWindowLongPtr(hWnd, GWL_USERDATA, NPT_POINTER_TO_LONG(this));

  return true;
}

void CDSPlayer::SetDsWndVisible(bool bVisible)
{
  int cmd;
  bVisible ? cmd = SW_SHOW : cmd = SW_HIDE;
  ShowWindow(m_hWnd, cmd);
  UpdateWindow(m_hWnd);
}

LRESULT CALLBACK CDSPlayer::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
  case WM_MOUSEMOVE:
  case WM_LBUTTONDOWN:
  case WM_MBUTTONDOWN:
  case WM_RBUTTONDOWN:
  case WM_LBUTTONUP:
  case WM_MBUTTONUP:
  case WM_RBUTTONUP:
  case WM_MOUSEWHEEL:
    ::PostMessage(g_hWnd, uMsg, wParam, lParam);
    return(0);
  case WM_SIZE:
    SetWindowPos(hWnd, 0, 0, 0, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER);
    return(0);
  }
  return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void CDSPlayer::HandleMessages()
{
  MSG msg;
  while (GetMessage(&msg, (HWND)-1, 0, 0) != 0 && msg.message == WM_GRAPHMESSAGE)
  {
    CDSMsg* pMsg = reinterpret_cast<CDSMsg *>(msg.lParam);
    if (!pMsg->IsType(CDSMsg::MADVR_SET_WINDOW_POS))
      CLog::Log(LOGDEBUG, "%s Message received : %d on thread 0x%X", __FUNCTION__, pMsg->GetMessageType(), m_threadID);

    if (CDSPlayer::PlayerState == DSPLAYER_CLOSED || CDSPlayer::PlayerState == DSPLAYER_LOADING)
    {
      pMsg->Set();
      pMsg->Release();
      break;
    }

    if (pMsg->IsType(CDSMsg::GENERAL_SET_WINDOW_POS))
    {
      g_dsGraph->UpdateWindowPosition();
    }
    else if (pMsg->IsType(CDSMsg::MADVR_SET_WINDOW_POS))
    {
      g_dsGraph->UpdateMadvrWindowPosition();
    }
    else if (pMsg->IsType(CDSMsg::PLAYER_SEEK_TIME))
    {
      CDSMsgPlayerSeekTime* speMsg = reinterpret_cast<CDSMsgPlayerSeekTime *>(pMsg);
      g_dsGraph->Seek(speMsg->GetTime(), speMsg->GetFlags(), speMsg->ShowPopup());
    }
    else if (pMsg->IsType(CDSMsg::PLAYER_SEEK))
    {
      CDSMsgPlayerSeek* speMsg = reinterpret_cast<CDSMsgPlayerSeek*>(pMsg);
      g_dsGraph->Seek(speMsg->Forward(), speMsg->LargeStep());
    }
    else if (pMsg->IsType(CDSMsg::PLAYER_SEEK_PERCENT))
    {
      CDSMsgDouble * speMsg = reinterpret_cast<CDSMsgDouble *>(pMsg);
      g_dsGraph->SeekPercentage((float)speMsg->m_value);
    }
    else if (pMsg->IsType(CDSMsg::PLAYER_PAUSE))
    {
      g_dsGraph->Pause();
    }
    else if (pMsg->IsType(CDSMsg::PLAYER_STOP))
    {
      CDSMsgBool* speMsg = reinterpret_cast<CDSMsgBool *>(pMsg);
      g_dsGraph->Stop(speMsg->m_value);
    }
    else if (pMsg->IsType(CDSMsg::PLAYER_PLAY))
    {
      CDSMsgBool* speMsg = reinterpret_cast<CDSMsgBool *>(pMsg);
      g_dsGraph->Play(speMsg->m_value);
    }
    else if (pMsg->IsType(CDSMsg::PLAYER_UPDATE_TIME))
    {
      g_dsGraph->UpdateTime();
    }

    /*DVD COMMANDS*/
    if (pMsg->IsType(CDSMsg::PLAYER_DVD_MOUSE_MOVE))
    {
      CDSMsgInt* speMsg = reinterpret_cast<CDSMsgInt *>(pMsg);
      //TODO make the xbmc gui stay hidden when moving mouse over menu
      POINT pt;
      pt.x = GET_X_LPARAM(speMsg->m_value);
      pt.y = GET_Y_LPARAM(speMsg->m_value);
      ULONG pButtonIndex;
      /**** Didnt found really where dvdplayer are doing it exactly so here it is *****/
      XBMC_Event newEvent;
      newEvent.type = XBMC_MOUSEMOTION;
      newEvent.motion.x = (uint16_t)pt.x;
      newEvent.motion.y = (uint16_t)pt.y;
      g_application.OnEvent(newEvent);
      /*CGUIMessage pMsg(GUI_MSG_VIDEO_MENU_STARTED, 0, 0);
      g_windowManager.SendMessage(pMsg);*/
      /**** End of ugly hack ***/
      if (SUCCEEDED(CGraphFilters::Get()->DVD.dvdInfo->GetButtonAtPosition(pt, &pButtonIndex)))
        CGraphFilters::Get()->DVD.dvdControl->SelectButton(pButtonIndex);
    }
    else if (pMsg->IsType(CDSMsg::PLAYER_DVD_MOUSE_CLICK))
    {
      CDSMsgInt* speMsg = reinterpret_cast<CDSMsgInt *>(pMsg);
      POINT pt;
      pt.x = GET_X_LPARAM(speMsg->m_value);
      pt.y = GET_Y_LPARAM(speMsg->m_value);
      ULONG pButtonIndex;
      if (SUCCEEDED(CGraphFilters::Get()->DVD.dvdInfo->GetButtonAtPosition(pt, &pButtonIndex)))
        CGraphFilters::Get()->DVD.dvdControl->SelectAndActivateButton(pButtonIndex);
    }
    else if (pMsg->IsType(CDSMsg::PLAYER_DVD_NAV_UP))
    {
      CGraphFilters::Get()->DVD.dvdControl->SelectRelativeButton(DVD_Relative_Upper);
    }
    else if (pMsg->IsType(CDSMsg::PLAYER_DVD_NAV_DOWN))
    {
      CGraphFilters::Get()->DVD.dvdControl->SelectRelativeButton(DVD_Relative_Lower);
    }
    else if (pMsg->IsType(CDSMsg::PLAYER_DVD_NAV_LEFT))
    {
      CGraphFilters::Get()->DVD.dvdControl->SelectRelativeButton(DVD_Relative_Left);
    }
    else if (pMsg->IsType(CDSMsg::PLAYER_DVD_NAV_RIGHT))
    {
      CGraphFilters::Get()->DVD.dvdControl->SelectRelativeButton(DVD_Relative_Right);
    }
    else if (pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_ROOT))
    {
      CGUIMessage _msg(GUI_MSG_VIDEO_MENU_STARTED, 0, 0);
      g_windowManager.SendMessage(_msg);
      CGraphFilters::Get()->DVD.dvdControl->ShowMenu(DVD_MENU_Root, DVD_CMD_FLAG_Block | DVD_CMD_FLAG_Flush, NULL);
    }
    else if (pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_EXIT))
    {
      CGraphFilters::Get()->DVD.dvdControl->Resume(DVD_CMD_FLAG_Block | DVD_CMD_FLAG_Flush, NULL);
    }
    else if (pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_BACK))
    {
      CGraphFilters::Get()->DVD.dvdControl->ReturnFromSubmenu(DVD_CMD_FLAG_Block | DVD_CMD_FLAG_Flush, NULL);
    }
    else if (pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_SELECT))
    {
      CGraphFilters::Get()->DVD.dvdControl->ActivateButton();
    }
    else if (pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_TITLE))
    {
      CGraphFilters::Get()->DVD.dvdControl->ShowMenu(DVD_MENU_Title, DVD_CMD_FLAG_Block | DVD_CMD_FLAG_Flush, NULL);
    }
    else if (pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_SUBTITLE))
    {
    }
    else if (pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_AUDIO))
    {
    }
    else if (pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_ANGLE))
    {
    }
    pMsg->Set();
    pMsg->Release();
  }
}

void CDSPlayer::Stop()
{
  PostMessage(new CDSMsgBool(CDSMsg::PLAYER_STOP, true));
}

void CDSPlayer::Pause()
{
  g_dsGraph->UpdateState();

  if (PlayerState == DSPLAYER_LOADING || PlayerState == DSPLAYER_LOADED)
    return;

  m_pGraphThread.SetSpeedChanged(true);
  if (PlayerState == DSPLAYER_PAUSED)
  {
    m_pGraphThread.SetCurrentRate(1);
    m_callback.OnPlayBackResumed();
  }
  else
  {
    m_pGraphThread.SetCurrentRate(0);
    m_callback.OnPlayBackPaused();
  }
  PostMessage(new CDSMsg(CDSMsg::PLAYER_PAUSE));
}
void CDSPlayer::ToFFRW(int iSpeed)
{
  if (iSpeed != 1)
    g_infoManager.SetDisplayAfterSeek();

  m_pGraphThread.SetCurrentRate(iSpeed);
  m_pGraphThread.SetSpeedChanged(true);
}

void CDSPlayer::Seek(bool bPlus, bool bLargeStep, bool bChapterOverride)
{
  PostMessage(new CDSMsgPlayerSeek(bPlus, bLargeStep));
}

void CDSPlayer::SeekPercentage(float iPercent)
{
  PostMessage(new CDSMsgDouble(CDSMsg::PLAYER_SEEK_PERCENT, iPercent));
}

bool CDSPlayer::OnAction(const CAction &action)
{
  if (g_dsGraph->IsDvd())
  {
    if (action.GetID() == ACTION_SHOW_VIDEOMENU)
    {
      PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_MENU_ROOT), false);
      return true;
    }
    if (g_dsGraph->IsInMenu())
    {
      switch (action.GetID())
      {
      case ACTION_PREVIOUS_MENU:
        PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_MENU_BACK), false);
        break;
      case ACTION_MOVE_LEFT:
        PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_NAV_LEFT), false);
        break;
      case ACTION_MOVE_RIGHT:
        PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_NAV_RIGHT), false);
        break;
      case ACTION_MOVE_UP:
        PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_NAV_UP), false);
        break;
      case ACTION_MOVE_DOWN:
        PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_NAV_DOWN), false);
        break;
        /*case ACTION_MOUSE_MOVE:
        case ACTION_MOUSE_LEFT_CLICK:
        {
        CRect rs, rd;
        GetVideoRect(rs, rd);
        CPoint pt(action.GetAmount(), action.GetAmount(1));
        if (!rd.PtInRect(pt))
        return false;
        pt -= CPoint(rd.x1, rd.y1);
        pt.x *= rs.Width() / rd.Width();
        pt.y *= rs.Height() / rd.Height();
        pt += CPoint(rs.x1, rs.y1);
        if (action.GetID() == ACTION_MOUSE_LEFT_CLICK)
        SendMessage(g_hWnd, WM_COMMAND, ID_DVD_MOUSE_CLICK,MAKELPARAM(pt.x,pt.y));
        else
        SendMessage(g_hWnd, WM_COMMAND, ID_DVD_MOUSE_MOVE,MAKELPARAM(pt.x,pt.y));
        return true;
        }
        break;*/
      case ACTION_SELECT_ITEM:
      {
        // show button pushed overlay
        PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_MENU_SELECT), false);
      }
        break;
      case REMOTE_0:
      case REMOTE_1:
      case REMOTE_2:
      case REMOTE_3:
      case REMOTE_4:
      case REMOTE_5:
      case REMOTE_6:
      case REMOTE_7:
      case REMOTE_8:
      case REMOTE_9:
      {
        // Offset from key codes back to button number
        // int button = action.actionId - REMOTE_0;
        //CLog::Log(LOGDEBUG, " - button pressed %d", button);
        //pStream->SelectButton(button);
      }
        break;
      default:
        return false;
        break;
      }
      return true; // message is handled
    }
  }

  if (g_pPVRStream)
  {
    switch (action.GetID())
    {
    case ACTION_MOVE_UP:
    case ACTION_NEXT_ITEM:
    case ACTION_CHANNEL_UP:
      if (SelectChannel(true))
      {
        g_infoManager.SetDisplayAfterSeek();
        ShowPVRChannelInfo();
      }
      else if (CDSPlayer::PlayerState == DSPLAYER_CLOSED)
      {
        m_callback.OnPlayBackStopped();
      }
      else
      {
        CLog::Log(LOGWARNING, "%s - failed to switch channel. playback stopped", __FUNCTION__);
        CApplicationMessenger::GetInstance().SendMsg(TMSG_MEDIA_STOP);
      }
      return true;
      break;

    case ACTION_MOVE_DOWN:
    case ACTION_PREV_ITEM:
    case ACTION_CHANNEL_DOWN:
      if (SelectChannel(false))
      {
        g_infoManager.SetDisplayAfterSeek();
        ShowPVRChannelInfo();
      }
      else if (CDSPlayer::PlayerState == DSPLAYER_CLOSED)
      {
        m_callback.OnPlayBackStopped();
      }
      else
      {
        CLog::Log(LOGWARNING, "%s - failed to switch channel. playback stopped", __FUNCTION__);
        CApplicationMessenger::GetInstance().SendMsg(TMSG_MEDIA_STOP);
      }
      return true;
      break;

    case ACTION_CHANNEL_SWITCH:
      // Offset from key codes back to button number
      int channel = (int)action.GetAmount();
      if (SwitchChannel(channel))
      {
        g_infoManager.SetDisplayAfterSeek();
        ShowPVRChannelInfo();
      }
      else if (CDSPlayer::PlayerState == DSPLAYER_CLOSED)
      {
        m_callback.OnPlayBackStopped();
      }
      else
      {
        CLog::Log(LOGWARNING, "%s - failed to switch channel. playback stopped", __FUNCTION__);
        CApplicationMessenger::GetInstance().SendMsg(TMSG_MEDIA_STOP);
      }
      return true;
      break;
    }
  }

  switch (action.GetID())
  {
  case ACTION_NEXT_ITEM:
  case ACTION_PAGE_UP:
    if (GetChapterCount() > 0)
    {
      SeekChapter(GetChapter() + 1);
      g_infoManager.SetDisplayAfterSeek();
      return true;
    }
    else
      break;
  case ACTION_PREV_ITEM:
  case ACTION_PAGE_DOWN:
    if (GetChapterCount() > 0)
    {
      SeekChapter(GetChapter() - 1);
      g_infoManager.SetDisplayAfterSeek();
      return true;
    }
    else
      break;
  }

  // return false to inform the caller we didn't handle the message
  return false;
}

void CDSPlayer::SetMadvrResolution()
{
  if (CSettings::GetInstance().GetInt(CSettings::SETTING_DSPLAYER_MANAGEMADVRWITHKODI) != KODIGUI_LOAD_DSPLAYER)
    return;

  CStreamDetails streamDetails;
  int res = CMadvrCallback::Get()->VideoDimsToResolution(GetPictureWidth(), GetPictureHeight());
  std::string str = g_application.CurrentFileItem().GetVideoInfoTag()->m_strShowTitle;

  CMediaSettings::GetInstance().GetCurrentMadvrSettings().m_Resolution = res;
  CMediaSettings::GetInstance().GetCurrentMadvrSettings().m_TvShowName = str;
}

// Time is in millisecond
void CDSPlayer::SeekTime(__int64 iTime)
{
  int seekOffset = (int)(iTime - DS_TIME_TO_MSEC(g_dsGraph->GetTime(true)));
  PostMessage(new CDSMsgPlayerSeekTime(MSEC_TO_DS_TIME((iTime < 0) ? 0 : iTime)));
  m_callback.OnPlayBackSeek((int)iTime, seekOffset);
}

// Time is in millisecond
bool CDSPlayer::SeekTimeRelative(__int64 iTime)
{
  __int64 abstime = (__int64)(DS_TIME_TO_MSEC(g_dsGraph->GetTime(true)) + iTime);
  PostMessage(new CDSMsgPlayerSeekTime(MSEC_TO_DS_TIME((abstime < 0) ? 0 : abstime)));
  m_callback.OnPlayBackSeek((int)abstime, (int)iTime);
  return true;
}

void CDSPlayer::UpdateApplication()
{
  if (g_pPVRStream)
  {
    CFileItem item(g_application.CurrentFileItem());
    if (g_pPVRStream->UpdateItem(item))
    {
      g_application.CurrentFileItem() = item;
      CApplicationMessenger::GetInstance().PostMsg(TMSG_UPDATE_CURRENT_ITEM, 0, -1, static_cast<void*>(new CFileItem(item)));
    }
  }
}

// Called after fast channel switch.
void CDSPlayer::UpdateChannelSwitchSettings()
{
  if (g_dsGraph->GetTime(true) + MSEC_TO_DS_TIME(3000) < g_dsGraph->GetTotalTime(true))
  {
    // Pause playback
    PostMessage(new CDSMsgBool(CDSMsg::PLAYER_PAUSE, true), false);
    // Seek to the end of timeshift buffer
    PostMessage(new CDSMsgPlayerSeekTime(g_dsGraph->GetTotalTime(true) - MSEC_TO_DS_TIME(3000)));
    // Start playback
    PostMessage(new CDSMsgBool(CDSMsg::PLAYER_PLAY, true), false);
  }

#ifdef HAS_VIDEO_PLAYBACK
  // when using fast channel switching some shortcuts are taken which 
  // means we'll have to update the view mode manually
  m_renderManager.SetViewMode(CMediaSettings::GetInstance().GetCurrentVideoSettings().m_ViewMode);
#endif
}

bool CDSPlayer::SwitchChannel(unsigned int iChannelNumber)
{
  m_PlayerOptions.identify = true;

  bool bResult = g_pPVRStream->SelectChannelByNumber(iChannelNumber);
  
  m_PlayerOptions.identify = false;

  return bResult;
}

bool CDSPlayer::SwitchChannel(const CPVRChannelPtr &channel)
{
  if (!g_PVRManager.CheckParentalLock(channel))
    return false;

  /* set GUI info */
  if (!g_PVRManager.PerformChannelSwitch(channel, true))
    return false;

  m_PlayerOptions.identify = true;

  bool bResult = g_pPVRStream->SelectChannel(channel);

  m_PlayerOptions.identify = false;

  return bResult;
}

bool CDSPlayer::SelectChannel(bool bNext)
{
  m_PlayerOptions.identify = true;

  bool bShowPreview = false;/*(CSettings::GetInstance().GetInt("pvrplayback.channelentrytimeout") > 0);*/ // TODO

  if (!bShowPreview)
  {
    g_infoManager.SetDisplayAfterSeek(100000);
  }

  bool bResult = (bNext ? g_pPVRStream->NextChannel(bShowPreview) : g_pPVRStream->PrevChannel(bShowPreview));
  
  m_PlayerOptions.identify = false;

  return bResult;
}

bool CDSPlayer::ShowPVRChannelInfo()
{
  bool bReturn(false);

  if (CSettings::GetInstance().GetInt(CSettings::SETTING_PVRMENU_DISPLAYCHANNELINFO) > 0)
  {
    g_PVRManager.ShowPlayerInfo(CSettings::GetInstance().GetInt(CSettings::SETTING_PVRMENU_DISPLAYCHANNELINFO));

    bReturn = true;
  }

  return bReturn;
}

bool CDSPlayer::CachePVRStream(void) const
{
  return g_pPVRStream && !g_PVRManager.IsPlayingRecording() && g_advancedSettings.m_bPVRCacheInVideoPlayer;
}

CDSGraphThread::CDSGraphThread(CDSPlayer * pPlayer)
  : m_pPlayer(pPlayer), CThread("CDSGraphThread thread")
{
  CoInitializeEx(NULL, COINIT_MULTITHREADED);
}

void CDSPlayer::FrameMove()
{
  m_renderManager.FrameMove();
  m_renderManager.UpdateResolution();
  m_renderManager.ManageCaptures();
}

void CDSPlayer::FrameWait(int ms)
{
  m_renderManager.FrameWait(ms);
}

bool CDSPlayer::HasFrame()
{
  return m_renderManager.HasFrame();
}

void CDSPlayer::Render(bool clear, uint32_t alpha, bool gui)
{
  m_renderManager.Render(clear, 0, alpha, gui);
}

void CDSPlayer::AfterRender()
{
  m_renderManager.FrameFinish();
}

void CDSPlayer::FlushRenderer()
{
  m_renderManager.Flush();
}

void CDSPlayer::SetRenderViewMode(int mode)
{
  m_renderManager.SetViewMode(mode);
}

float CDSPlayer::GetRenderAspectRatio()
{
  return m_renderManager.GetAspectRatio();
}

RESOLUTION CDSPlayer::GetRenderResolution()
{
  return g_graphicsContext.GetVideoResolution();
}

bool CDSPlayer::IsRenderingVideo()
{
  return m_renderManager.IsConfigured();
}

bool CDSPlayer::IsRenderingGuiLayer()
{
  return m_renderManager.IsGuiLayer();
}

bool CDSPlayer::IsRenderingVideoLayer()
{
  return m_renderManager.IsVideoLayer();
}

bool CDSPlayer::Supports(EDEINTERLACEMODE mode)
{
  return m_renderManager.Supports(mode);
}

bool CDSPlayer::Supports(EINTERLACEMETHOD method)
{
  return m_renderManager.Supports(method);
}

bool CDSPlayer::Supports(ESCALINGMETHOD method)
{
  return m_renderManager.Supports(method);
}

bool CDSPlayer::Supports(ERENDERFEATURE feature)
{
  return m_renderManager.Supports(feature);
}

void CDSGraphThread::OnStartup()
{
  m_threadID = CThread::GetCurrentThreadId();
}

void CDSGraphThread::Process()
{
  START_PERFORMANCE_COUNTER
    m_pPlayer->m_hDSGraph = g_dsGraph->SetFile(m_pPlayer->currentFileItem, m_pPlayer->m_PlayerOptions);
  END_PERFORMANCE_COUNTER("Loading file");
  m_pPlayer->m_hDSGraphEvent.Set();

  while (!m_bStop && CDSPlayer::PlayerState != DSPLAYER_CLOSED && CDSPlayer::PlayerState != DSPLAYER_LOADING)
    HandleMessages();
}

void CDSGraphThread::HandleMessages()
{
  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0) != 0)
  {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}

void CDSGraphThread::OnExit()
{
  // In case of, set the ready event
  // Prevent a dead loop
  m_pPlayer->m_hDSGraphEvent.Set();
  m_bStop = true;
  m_threadID = 0;
  CoUninitialize();
  CLog::Log(LOGNOTICE, "thread end: CDSGraphThread::OnExit()");
}

CGraphManagementThread::CGraphManagementThread(CDSPlayer * pPlayer)
  : m_pPlayer(pPlayer), m_bSpeedChanged(false), CThread("CGraphManagementThread thread")
{
}

void CGraphManagementThread::OnStartup()
{
}

void CGraphManagementThread::Process()
{

  while (!this->m_bStop)
  {

    if (CDSPlayer::PlayerState == DSPLAYER_CLOSED)
      break;
    if (m_bSpeedChanged)
    {
      m_pPlayer->GetClock().SetSpeed(m_currentRate * 1000);
      m_clockStart = m_pPlayer->GetClock().GetClock();
      m_bSpeedChanged = false;

      if (m_currentRate == 1)
        g_dsGraph->Play();

    }
    if (CDSPlayer::PlayerState == DSPLAYER_CLOSED)
      break;
    // Handle Rewind or Fast Forward
    if (m_currentRate != 1)
    {
      double clock = m_pPlayer->GetClock().GetClock() - m_clockStart; // Time elapsed since the rate change
      // Only seek if elapsed time is greater than 250 ms
      if (abs(DS_TIME_TO_MSEC(clock)) >= 250)
      {
        //CLog::Log(LOGDEBUG, "Seeking time : %f", DS_TIME_TO_MSEC(clock));

        // New position
        uint64_t newPos = g_dsGraph->GetTime() + (uint64_t)clock;
        //CLog::Log(LOGDEBUG, "New position : %f", DS_TIME_TO_SEC(newPos));

        // Check boundaries
        if (newPos <= 0)
        {
          newPos = 0;
          m_currentRate = 1;
          m_pPlayer->GetPlayerCallback().OnPlayBackSpeedChanged(1);
          m_bSpeedChanged = true;
        }
        else if (newPos >= g_dsGraph->GetTotalTime())
        {
          CApplicationMessenger::GetInstance().SendMsg(TMSG_MEDIA_STOP);
          break;
        }

        g_dsGraph->Seek(newPos);

        m_clockStart = m_pPlayer->GetClock().GetClock();
      }
    }
    if (CDSPlayer::PlayerState == DSPLAYER_CLOSED)
      break;

    // Handle rare graph event
    g_dsGraph->HandleGraphEvent();

    // Update displayed time
    g_dsGraph->UpdateTime();

    Sleep(250);
    if (CDSPlayer::PlayerState == DSPLAYER_CLOSED)
      break;
  }
}
#endif
