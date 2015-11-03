#pragma once

/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
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

#ifndef HAS_DS_PLAYER
#error DSPlayer's header file included without HAS_DS_PLAYER defined
#endif

#include "cores/VideoPlayer/Videorenderers/BaseRenderer.h"
#include "IPaintCallback.h"

typedef enum _RENDERERTYPE
{
  RENDERER_UNINIT = 0,
  RENDERER_NORMAL = 1,
  RENDERER_DSHOW  = 2
} RENDERERTYPE;

class CBaseDSRenderer : public CBaseRenderer
{
public:
  virtual void         RegisterCallback(IPaintCallback *callback){};
  virtual void         UnregisterCallback(){};
  virtual inline void  OnAfterPresent(){};
protected:
  void ManageDisplay();
};

