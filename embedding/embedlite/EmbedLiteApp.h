/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef EMBED_LITE_APP_H
#define EMBED_LITE_APP_H

#include "mozilla/RefPtr.h"
#include "nsStringGlue.h"
#include <stdint.h>
#include <map>

namespace mozilla {
namespace embedlite {

typedef void (*EMBEDTaskCallback)(void* userData);

class EmbedLiteUILoop;
class EmbedLiteSubThread;
class EmbedLiteAppThread;
class EmbedLiteView;
class EmbedLiteAppListener
{
public:
  // StartChildThread must be called in case of native thread, otherwise return false
  virtual bool ExecuteChildThread() {
    return false;
  }
  // Native thread must be stopped here
  virtual bool StopChildThread() {
    return false;
  }
  // App Initialized and ready to API call
  virtual void Initialized() {}
  // App Destroyed, and ready to delete and program exit
  virtual void Destroyed() {}
  // Messaging interface, allow to receive json messages from content child scripts
  virtual void OnObserve(const char* aMessage, const PRUnichar* aData) {}
};

class EmbedLiteApp
{
public:
  virtual ~EmbedLiteApp();

  enum EmbedType {
    EMBED_INVALID,// Default value
    EMBED_THREAD, // Initialize XPCOM in child thread
    EMBED_PROCESS // Initialize XPCOM in separate process
  };

  enum RenderType {
    RENDER_AUTO,// Default value
    RENDER_SW, // Initialize XPCOM in child thread
    RENDER_HW // Initialize XPCOM in separate process
  };

  // Set Listener interface for EmbedLiteApp notifications
  virtual void SetListener(EmbedLiteAppListener* aListener);

  // Public Embedding API

  virtual EmbedType GetType() {
    return mEmbedType;
  }

  // Delayed post task helper for delayed functions call in main thread
  virtual void* PostTask(EMBEDTaskCallback callback, void* userData, int timeout = 0);
  virtual void CancelTask(void* aTask);

  // Start UI embedding loop merged with Gecko GFX, blocking call until Stop() called
  virtual bool Start(EmbedType aEmbedType);
  // Exit from UI embedding loop started with Start()
  virtual void Stop();

  // Specify path to Gecko components manifest location
  virtual void AddManifestLocation(const char* manifest);

  // This must be called in native toolkit child thread, only after ExecuteChildThread call
  virtual bool StartChildThread();
  // Must be called from same thread as StartChildThread, and before Stop()
  virtual bool StopChildThread();

  virtual EmbedLiteView* CreateView();
  virtual void DestroyView(EmbedLiteView* aView);

  virtual void SetIsAccelerated(bool aIsAccelerated);
  virtual bool IsAccelerated() {
    return mRenderType == RENDER_HW ? true : false ;
  }
  virtual RenderType GetRenderType() {
    return mRenderType;
  }

  // Setup preferences
  virtual void SetBoolPref(const char* aName, bool aValue);
  virtual void SetCharPref(const char* aName, const char* aValue);
  virtual void SetIntPref(const char* aName, int aValue);

  virtual void LoadGlobalStyleSheet(const char* aUri, bool aEnable);

  // Observer interface
  virtual void SendObserve(const char* aMessageName, const PRUnichar* aMessage);
  virtual void AddObserver(const char* aMessageName);
  virtual void RemoveObserver(const char* aMessageName);

  // Internal
  EmbedLiteAppListener* GetListener() {
    return mListener;
  }

  // Only one EmbedHelper object allowed
  static EmbedLiteApp* GetInstance();

private:
  EmbedLiteApp();

  static void StartChild(EmbedLiteApp* aApp);

  friend class EmbedLiteAppThreadParent;
  friend class EmbedLiteViewThreadParent;
  friend class EmbedLiteCompositorParent;
  EmbedLiteView* GetViewByID(uint32_t id);
  void ViewDestroyed(uint32_t id);
  void ChildReadyToDestroy();

  static EmbedLiteApp* sSingleton;
  EmbedLiteAppListener* mListener;
  EmbedLiteUILoop* mUILoop;
  RefPtr<EmbedLiteSubThread> mSubThread;
  EmbedType mEmbedType;
  RefPtr<EmbedLiteAppThread> mAppThread;
  std::map<uint32_t, EmbedLiteView*> mViews;
  uint32_t mViewCreateID;
  bool mDestroying;
  RenderType mRenderType;
};

} // namespace embedlite
} // namespace mozilla

#endif // EMBED_LITE_APP_H
