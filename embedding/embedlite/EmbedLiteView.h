/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZ_VIEW_EMBED_H
#define MOZ_VIEW_EMBED_H

#include "mozilla/RefPtr.h"
#include "nsStringGlue.h"
#include "gfxMatrix.h"
#include "nsRect.h"
#include "InputData.h"

namespace mozilla {
namespace embedlite {

class EmbedLiteViewImplIface;
class EmbedLiteView;

class EmbedLiteViewListener
{
public:
  // View intialized and ready for API calls
  virtual void ViewInitialized() {}
  // View finally destroyed and deleted
  virtual void ViewDestroyed() {}

  // Messaging interface, allow to receive json messages from content child scripts
  virtual void RecvAsyncMessage(const PRUnichar* aMessage, const PRUnichar* aData) {}
  virtual char* RecvSyncMessage(const PRUnichar* aMessage, const PRUnichar* aData) { return NULL; }

  virtual void OnLocationChanged(const char* aLocation, bool aCanGoBack, bool aCanGoForward) {}
  virtual void OnLoadStarted(const char* aLocation) {}
  virtual void OnLoadFinished(void) {}
  virtual void OnLoadRedirect(void) {}
  virtual void OnLoadProgress(int32_t aProgress, int32_t aCurTotal, int32_t aMaxTotal) {}
  virtual void OnSecurityChanged(const char* aStatus, unsigned int aState) {}
  virtual void OnFirstPaint(int32_t aX, int32_t aY) {}
  virtual void OnScrolledAreaChanged(unsigned int aWidth, unsigned int aHeight) {}
  virtual void OnScrollChanged(int32_t offSetX, int32_t offSetY) {}
  virtual void SetBackgroundColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {}

  // Compositor Interface
  //   Invalidate notification
  virtual bool Invalidate() { return false; }
  virtual void SetFirstPaintViewport(const nsIntPoint& aOffset, float aZoom,
                                     const nsIntRect& aPageRect, const gfxRect& aCssPageRect) {}
  virtual void SyncViewportInfo(const nsIntRect& aDisplayPort,
                                float aDisplayResolution, bool aLayersUpdated,
                                nsIntPoint& aScrollOffset, float& aScaleX, float& aScaleY) {}
  virtual void SetPageRect(const gfxRect& aCssPageRect) {}
  virtual void IMENotification(int aEnabled, bool aOpen, int aCause, int aFocusChange) {}

  // AZPC Interface, return true in order to prevent default behavior
  virtual bool RequestContentRepaint() { return false; }
  virtual bool HandleDoubleTap(const nsIntPoint& aPoint) { return false; }
  virtual bool HandleSingleTap(const nsIntPoint& aPoint) { return false; }
  virtual bool HandleLongTap(const nsIntPoint& aPoint) { return false; }
  virtual bool SendAsyncScrollDOMEvent(const gfxRect& aContentRect,
                                       const gfxSize& aScrollableSize) { return false; }
};

class EmbedLiteApp;
class EmbedLiteView
{
public:
  EmbedLiteView(EmbedLiteApp* aApp);
  virtual ~EmbedLiteView();

  // Listener setup
  virtual void SetListener(EmbedLiteViewListener* aListener) { mListener = aListener; }
  virtual EmbedLiteViewListener* GetListener() { return mListener; }

  // Embed Interface
  virtual void LoadURL(const char* aUrl);
  virtual void SetIsActive(bool);
  virtual void SuspendTimeouts();
  virtual void ResumeTimeouts();
  virtual void GoBack();
  virtual void GoForward();
  virtual void StopLoad();
  virtual void Reload(bool hard);

  // Input Interface
  enum PanZoomControlType { EXTERNAL, GECKO_SIMPLE, GECKO_TOUCH };
  virtual void SetPanZoomControlType(PanZoomControlType aType);

  virtual void SendTextEvent(const char* composite, const char* preEdit);
  virtual void SendKeyPress(int domKeyCode, int gmodifiers, int charCode);
  virtual void SendKeyRelease(int domKeyCode, int gmodifiers, int charCode);

  virtual void ReceiveInputEvent(const mozilla::InputData& aEvent);
  virtual void MousePress(int x, int y, int mstime, unsigned int buttons, unsigned int modifiers);
  virtual void MouseRelease(int x, int y, int mstime, unsigned int buttons, unsigned int modifiers);
  virtual void MouseMove(int x, int y, int mstime, unsigned int buttons, unsigned int modifiers);

  virtual void PinchStart(int x, int y);
  virtual void PinchUpdate(int x, int y, float scale);
  virtual void PinchEnd(int x, int y, float scale);

  // Setup renderable view size
  virtual void SetViewSize(int width, int height);

  // Scroll/Zoom API
  virtual bool ScrollBy(int aDX, int aDY, bool aDoOverflow = false);

  // Compositor Interface
  //   PNG Decoded data
  virtual char* GetImageAsURL(int aWidth = -1, int aHeight = -1);

  // Render content into custom rgb image (SW Rendering)
  virtual bool RenderToImage(unsigned char* aData, int imgW, int imgH, int stride, int depth);

  //   GL Rendering setuo
  virtual bool RenderGL();
  //   Setup renderable GL/EGL window surface size
  virtual void SetGLViewPortSize(int width, int height);
  //   GL world transform offset and simple rotation are allowed (orientation change)
  virtual void SetGLViewTransform(gfxMatrix matrix);
  //   Setup Clipping on view area, required if view gl area need to be particulary clipped withing target widget area
  virtual void SetViewClipping(float aX, float aY, float aWidth, float aHeight);

  // Set Custom transform for compositor layers tree, Fast Scroll/Zoom
  virtual void SetTransformation(float aScale, nsIntPoint aScrollOffset);
  virtual void ScheduleRender();

  // Scripting Interface, allow to extend embedding API by creating
  // child js scripts and messaging interface.
  // and do communication between UI and Content child via json messages.
  // See RecvAsyncMessage, RecvSyncMessage, SendAsyncMessage
  // For more detailed info see https://developer.mozilla.org/en-US/docs/The_message_manager
  //   https://wiki.mozilla.org/Content_Process_Event_Handlers
  virtual void LoadFrameScript(const char* aURI);

  virtual void AddMessageListener(const char* aMessageName);
  virtual void RemoveMessageListener(const char* aMessageName);
  virtual void SendAsyncMessage(const PRUnichar* aMessageName, const PRUnichar* aMessage);

  virtual uint32_t GetUniqueID();

private:
  friend class EmbedLiteViewThreadParent;
  friend class EmbedLiteCompositorParent;
  void SetImpl(EmbedLiteViewImplIface*);
  EmbedLiteViewImplIface* GetImpl();
  PanZoomControlType GetPanZoomControlType() { return mPanControlType; }

  EmbedLiteApp* mApp;
  EmbedLiteViewListener* mListener;
  EmbedLiteViewImplIface* mViewImpl;
  PanZoomControlType mPanControlType;
};

} // namespace embedlite
} // namespace mozilla

#endif
