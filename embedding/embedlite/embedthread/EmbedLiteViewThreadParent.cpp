/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#define LOG_COMPONENT "EmbedLiteViewThreadParent"
#include "EmbedLog.h"

#include "EmbedLiteViewThreadParent.h"
#include "EmbedLiteApp.h"
#include "EmbedLiteView.h"

#include "EmbedLiteCompositorParent.h"
#include "mozilla/unused.h"
#include "mozilla/layers/AsyncPanZoomController.h"
#include "mozilla/layers/GeckoContentController.h"

using namespace mozilla::layers;
using namespace mozilla::widget;

namespace mozilla {
namespace embedlite {

class EmbedContentController : public GeckoContentController
{
  public:
    EmbedContentController(EmbedLiteViewThreadParent* aRenderFrame)
      : mUILoop(MessageLoop::current())
      , mRenderFrame(aRenderFrame)
    { }

    virtual void RequestContentRepaint(const FrameMetrics& aFrameMetrics) MOZ_OVERRIDE {
      // We always need to post requests into the "UI thread" otherwise the
      // requests may get processed out of order.
      LOGT();
      mUILoop->PostTask(
        FROM_HERE,
        NewRunnableMethod(this, &EmbedContentController::DoRequestContentRepaint,
      aFrameMetrics));
    }

    virtual void HandleDoubleTap(const nsIntPoint& aPoint) MOZ_OVERRIDE {
      if (MessageLoop::current() != mUILoop) {
        // We have to send this message from the "UI thread" (main
        // thread).
        mUILoop->PostTask(
          FROM_HERE,
          NewRunnableMethod(this, &EmbedContentController::HandleDoubleTap,
        aPoint));
        return;
      }
      EmbedLiteViewListener* listener = GetListener();
      if (listener && !listener->HandleDoubleTap(aPoint)) {
        unused << mRenderFrame->SendHandleDoubleTap(aPoint);
      }
    }

    virtual void HandleSingleTap(const nsIntPoint& aPoint) MOZ_OVERRIDE {
      if (MessageLoop::current() != mUILoop) {
        // We have to send this message from the "UI thread" (main
        // thread).
        mUILoop->PostTask(
          FROM_HERE,
          NewRunnableMethod(this, &EmbedContentController::HandleSingleTap,
        aPoint));
        return;
      }
      EmbedLiteViewListener* listener = GetListener();
      if (listener && !listener->HandleSingleTap(aPoint)) {
        unused << mRenderFrame->SendHandleSingleTap(aPoint);
      }
    }

    virtual void HandleLongTap(const nsIntPoint& aPoint) MOZ_OVERRIDE {
      if (MessageLoop::current() != mUILoop) {
        // We have to send this message from the "UI thread" (main
        // thread).
        mUILoop->PostTask(
          FROM_HERE,
          NewRunnableMethod(this, &EmbedContentController::HandleLongTap,
        aPoint));
        return;
      }
      EmbedLiteViewListener* listener = GetListener();
      if (listener && !listener->HandleLongTap(aPoint)) {
        unused << mRenderFrame->SendHandleLongTap(aPoint);
      }
    }

    /**
     * Requests sending a mozbrowserasyncscroll domevent to embedder.
     * |aContentRect| is in CSS pixels, relative to the current cssPage.
     * |aScrollableSize| is the current content width/height in CSS pixels.
     */
    virtual void SendAsyncScrollDOMEvent(const gfx::Rect& aContentRect,
                                         const gfx::Size& aScrollableSize) {
      LOGNI("contentR[%g,%g,%g,%g], scrSize[%g,%g]",
            aContentRect.x, aContentRect.y, aContentRect.width, aContentRect.height,
            aScrollableSize.width, aScrollableSize.height);
      gfxRect rect(aContentRect.x, aContentRect.y, aContentRect.width, aContentRect.height);
      gfxSize size(aScrollableSize.width, aScrollableSize.height);
      EmbedLiteViewListener* listener = GetListener();
      if (listener && !listener->SendAsyncScrollDOMEvent(rect, size)) {
        unused << mRenderFrame->SendAsyncScrollDOMEvent(rect, size);
      }
    }

    void ClearRenderFrame() {
      mRenderFrame = nullptr;
    }

  private:
    EmbedLiteViewListener* GetListener() {
      return mRenderFrame && mRenderFrame->mView ?
             mRenderFrame->mView->GetListener() : nullptr;
    }

    void DoRequestContentRepaint(const FrameMetrics& aFrameMetrics) {
      EmbedLiteViewListener* listener = GetListener();
      if (listener && !listener->RequestContentRepaint()) {
        unused << mRenderFrame->SendUpdateFrame(aFrameMetrics);
      }
    }

    MessageLoop* mUILoop;
    EmbedLiteViewThreadParent* mRenderFrame;
};

EmbedLiteViewThreadParent::EmbedLiteViewThreadParent(const uint32_t& id)
  : mId(id)
  , mView(EmbedLiteApp::GetInstance()->GetViewByID(id))
  , mViewAPIDestroyed(false)
  , mCompositor(nullptr)
  , mScrollOffset(0, 0)
  , mLastScale(1.0f)
  , mInTouchProcess(false)
  , mUILoop(MessageLoop::current())
  , mLastIMEState(0)
{
  MOZ_COUNT_CTOR(EmbedLiteViewThreadParent);
  NS_ASSERTION(mView, "View not found");
  mView->SetImpl(this);
  mGeckoController = new EmbedContentController(this);
}

EmbedLiteViewThreadParent::~EmbedLiteViewThreadParent()
{
  MOZ_COUNT_DTOR(EmbedLiteViewThreadParent);
  LOGT();
  if (mGeckoController) {
    mGeckoController->ClearRenderFrame();
  }
  if (mController) {
    mController->Destroy();
  }
  if (mView) {
    mView->SetImpl(NULL);
  }
}

void
EmbedLiteViewThreadParent::ActorDestroy(ActorDestroyReason aWhy)
{
  LOGT("reason:%i", aWhy);
  if (mGeckoController) {
    mGeckoController->ClearRenderFrame();
  }
  if (mController) {
    mController->Destroy();
  }
}

void
EmbedLiteViewThreadParent::SetCompositor(EmbedLiteCompositorParent* aCompositor)
{
  LOGT();
  mCompositor = aCompositor;
  UpdateScrollController();
  SetGLViewPortSize(mGLViewPortSize.width, mGLViewPortSize.height);
}

void
EmbedLiteViewThreadParent::UpdateScrollController()
{
  mController = nullptr;
  if (mViewAPIDestroyed) {
    return;
  }

  NS_ENSURE_TRUE(mView, );
  if (mView->GetPanZoomControlType() != EmbedLiteView::PanZoomControlType::EXTERNAL) {
    AsyncPanZoomController::GestureBehavior type;
    if (mView->GetPanZoomControlType() == EmbedLiteView::PanZoomControlType::GECKO_SIMPLE) {
      type = AsyncPanZoomController::DEFAULT_GESTURES;
    } else if (mView->GetPanZoomControlType() == EmbedLiteView::PanZoomControlType::GECKO_TOUCH) {
      type = AsyncPanZoomController::USE_GESTURE_DETECTOR;
    } else {
      return;
    }
    mController = new AsyncPanZoomController(mGeckoController, type);
    mController->SetCompositorParent(mCompositor);
    mController->UpdateCompositionBounds(nsIntRect(0, 0, mViewSize.width, mViewSize.height));
  }
}

AsyncPanZoomController*
EmbedLiteViewThreadParent::GetDefaultPanZoomController()
{
  return mController;
}

// Child notification

bool
EmbedLiteViewThreadParent::RecvInitialized()
{
  if (mViewAPIDestroyed) {
    return true;
  }

  NS_ENSURE_TRUE(mView, false);
  mView->GetListener()->ViewInitialized();
  return true;
}

bool
EmbedLiteViewThreadParent::RecvOnLocationChanged(const nsCString& aLocation,
                                                 const bool& aCanGoBack,
                                                 const bool& aCanGoForward)
{
  LOGNI();
  if (mViewAPIDestroyed) {
    return true;
  }

  NS_ENSURE_TRUE(mView, false);
  mView->GetListener()->OnLocationChanged(aLocation.get(), aCanGoBack, aCanGoForward);
  return true;
}

bool
EmbedLiteViewThreadParent::RecvOnLoadStarted(const nsCString& aLocation)
{
  LOGNI();
  if (mViewAPIDestroyed) {
    return true;
  }

  NS_ENSURE_TRUE(mView, false);
  mView->GetListener()->OnLoadStarted(aLocation.get());
  return true;
}

bool
EmbedLiteViewThreadParent::RecvOnLoadFinished()
{
  LOGNI();
  if (mViewAPIDestroyed) {
    return true;
  }

  NS_ENSURE_TRUE(mView, false);
  mView->GetListener()->OnLoadFinished();
  return true;
}

bool
EmbedLiteViewThreadParent::RecvOnLoadRedirect()
{
  LOGNI();
  if (mViewAPIDestroyed) {
    return true;
  }

  NS_ENSURE_TRUE(mView, false);
  mView->GetListener()->OnLoadRedirect();
  return true;
}

bool
EmbedLiteViewThreadParent::RecvOnLoadProgress(const int32_t& aProgress, const int32_t& aCurTotal, const int32_t& aMaxTotal)
{
  LOGNI("progress:%i", aProgress);
  NS_ENSURE_TRUE(mView, false);
  if (mViewAPIDestroyed) {
    return true;
  }

  mView->GetListener()->OnLoadProgress(aProgress, aCurTotal, aMaxTotal);
  return true;
}

bool
EmbedLiteViewThreadParent::RecvOnSecurityChanged(const nsCString& aStatus,
                                                 const uint32_t& aState)
{
  LOGNI();
  if (mViewAPIDestroyed) {
    return true;
  }

  NS_ENSURE_TRUE(mView, false);
  mView->GetListener()->OnSecurityChanged(aStatus.get(), aState);
  return true;
}

bool
EmbedLiteViewThreadParent::RecvOnFirstPaint(const int32_t& aX,
                                            const int32_t& aY)
{
  LOGNI();
  if (mViewAPIDestroyed) {
    return true;
  }

  NS_ENSURE_TRUE(mView, false);
  mView->GetListener()->OnFirstPaint(aX, aY);
  return true;
}

bool
EmbedLiteViewThreadParent::RecvOnScrolledAreaChanged(const uint32_t& aWidth,
                                                     const uint32_t& aHeight)
{
  LOGNI("area[%u,%u]", aWidth, aHeight);
  if (mViewAPIDestroyed) {
    return true;
  }

  NS_ENSURE_TRUE(mView, false);
  mView->GetListener()->OnScrolledAreaChanged(aWidth, aHeight);
  return true;
}

bool
EmbedLiteViewThreadParent::RecvOnScrollChanged(const int32_t& offSetX,
                                               const int32_t& offSetY)
{
  LOGNI("off[%i,%i]", offSetX, offSetY);
  if (mViewAPIDestroyed) {
    return true;
  }

  NS_ENSURE_TRUE(mView, false);
  mView->GetListener()->OnScrollChanged(offSetX, offSetY);
  return true;
}

bool
EmbedLiteViewThreadParent::RecvUpdateZoomConstraints(const bool& val, const float& min, const float& max)
{
  if (mController) {
    mController->UpdateZoomConstraints(val, min, max);
  }
  return true;
}

bool
EmbedLiteViewThreadParent::RecvZoomToRect(const gfxRect& aRect)
{
  if (mController) {
    mController->ZoomToRect(aRect);
  }
  return true;
}

bool
EmbedLiteViewThreadParent::RecvCancelDefaultPanZoom()
{
  if (mController && mInTouchProcess) {
    mController->CancelDefaultPanZoom();
  }
  return true;
}

bool
EmbedLiteViewThreadParent::RecvContentReceivedTouch(const bool& aPreventDefault)
{
  if (mController) {
    mController->ContentReceivedTouch(aPreventDefault);
  }
  return true;
}

bool
EmbedLiteViewThreadParent::RecvDetectScrollableSubframe()
{
  if (mController) {
    mController->DetectScrollableSubframe();
  }
  return true;
}

bool
EmbedLiteViewThreadParent::RecvSetBackgroundColor(const nscolor& aColor)
{
  if (mViewAPIDestroyed) {
    return true;
  }

  NS_ENSURE_TRUE(mView, false);
  mView->GetListener()->SetBackgroundColor(NS_GET_R(aColor), NS_GET_G(aColor), NS_GET_B(aColor), NS_GET_A(aColor));
  return true;
}

// Incoming API calls

void
EmbedLiteViewThreadParent::LoadURL(const char* aUrl)
{
  LOGT("url:%s", aUrl);
  unused << SendLoadURL(NS_ConvertUTF8toUTF16(nsDependentCString(aUrl)));
}

void EmbedLiteViewThreadParent::GoBack()
{
  unused << SendGoBack();
}

void EmbedLiteViewThreadParent::GoForward()
{
  unused << SendGoForward();
}

void EmbedLiteViewThreadParent::StopLoad()
{
  unused << SendStopLoad();
}

void EmbedLiteViewThreadParent::Reload(bool hardReload)
{
  unused << SendReload(hardReload);
}

void
EmbedLiteViewThreadParent::SetIsActive(bool aIsActive)
{
  LOGF();
  unused << SendSetIsActive(aIsActive);
}

void
EmbedLiteViewThreadParent::SuspendTimeouts()
{
  LOGF();
  unused << SendSuspendTimeouts();
}

void
EmbedLiteViewThreadParent::ResumeTimeouts()
{
  LOGF();
  unused << SendResumeTimeouts();
}

void
EmbedLiteViewThreadParent::LoadFrameScript(const char* aURI)
{
  LOGT("uri:%s", aURI);
  unused << SendLoadFrameScript(NS_ConvertUTF8toUTF16(nsDependentCString(aURI)));
}

void
EmbedLiteViewThreadParent::DoSendAsyncMessage(const PRUnichar* aMessageName, const PRUnichar* aMessage)
{
  LOGT("msgName:%ls, msg:%ls", aMessageName, aMessage);
  const nsDependentString msgname(aMessageName);
  const nsDependentString msg(aMessage);
  unused << SendAsyncMessage(msgname,
                             msg);
}

void
EmbedLiteViewThreadParent::AddMessageListener(const char* aMessageName)
{
  LOGT("msgName:%s", aMessageName);
  unused << SendAddMessageListener(nsDependentCString(aMessageName));
}

void
EmbedLiteViewThreadParent::RemoveMessageListener(const char* aMessageName)
{
  LOGT("msgName:%s", aMessageName);
  unused << SendRemoveMessageListener(nsDependentCString(aMessageName));
}

bool
EmbedLiteViewThreadParent::RecvAsyncMessage(const nsString& aMessage,
                                            const nsString& aData)
{
  if (mViewAPIDestroyed) {
    return true;
  }

  LOGF("msg:%s, data:%s", NS_ConvertUTF16toUTF8(aMessage).get(), NS_ConvertUTF16toUTF8(aData).get());

  NS_ENSURE_TRUE(mView, false);
  mView->GetListener()->RecvAsyncMessage(aMessage.get(), aData.get());
  return true;
}

bool
EmbedLiteViewThreadParent::RecvSyncMessage(const nsString& aMessage,
                                           const nsString& aJSON,
                                           InfallibleTArray<nsString>* aJSONRetVal)
{
  LOGT("msg:%s, data:%s", NS_ConvertUTF16toUTF8(aMessage).get(), NS_ConvertUTF16toUTF8(aJSON).get());
  if (mViewAPIDestroyed) {
    return true;
  }

  NS_ENSURE_TRUE(mView, false);
  char* retval =
    mView->GetListener()->
    RecvSyncMessage(aMessage.get(), aJSON.get());
  if (retval) {
    aJSONRetVal->AppendElement(NS_ConvertUTF8toUTF16(nsDependentCString(retval)));
    delete retval;
  }
  return true;
}

static inline gfxASurface::gfxImageFormat
_depth_to_gfxformat(int depth)
{
  switch (depth) {
    case 32:
      return gfxASurface::ImageFormatARGB32;
    case 24:
      return gfxASurface::ImageFormatRGB24;
    case 16:
      return gfxASurface::ImageFormatRGB16_565;
    default:
      return gfxASurface::ImageFormatUnknown;
  }
}

bool
EmbedLiteViewThreadParent::RenderToImage(unsigned char* aData, int imgW, int imgH, int stride, int depth)
{
  LOGF("d:%p, sz[%i,%i], stride:%i, depth:%i", aData, imgW, imgH, stride, depth);
  if (mCompositor) {
    nsRefPtr<gfxImageSurface> source =
      new gfxImageSurface(aData, gfxIntSize(imgW, imgH), stride, _depth_to_gfxformat(depth));
    {
      nsRefPtr<gfxContext> context = new gfxContext(source);
      return mCompositor->RenderToContext(context);
    }
  }
  return false;
}

bool
EmbedLiteViewThreadParent::RenderGL()
{
  if (mCompositor) {
    return mCompositor->RenderGL();
  }
  return false;
}

bool
EmbedLiteViewThreadParent::ScrollBy(int aDX, int aDY, bool aDoOverflow)
{
  LOGT("d[%i,%i]", aDX, aDY);
  mScrollOffset.MoveBy(-aDX, -aDY);
  if (mCompositor) {
    mCompositor->SetTransformation(mLastScale, nsIntPoint(mScrollOffset.x, mScrollOffset.y));
    mCompositor->ScheduleRenderOnCompositorThread();
  }

  return true;
}

void
EmbedLiteViewThreadParent::SetViewSize(int width, int height)
{
  LOGT("sz[%i,%i]", width, height);
  mViewSize = gfxSize(width, height);
  unused << SendSetViewSize(mViewSize);
  if (mController) {
    mController->UpdateCompositionBounds(nsIntRect(0, 0, width, height));
  }
}

void
EmbedLiteViewThreadParent::SetGLViewPortSize(int width, int height)
{
  mGLViewPortSize = gfxSize(width, height);
  if (mCompositor) {
    mCompositor->SetSurfaceSize(width, height);
  }
}

void
EmbedLiteViewThreadParent::SetGLViewTransform(gfxMatrix matrix)
{
  if (mCompositor) {
    mCompositor->SetWorldTransform(matrix);
  }
}

void
EmbedLiteViewThreadParent::SetViewClipping(const gfxRect& aClipRect)
{
  if (mCompositor) {
    mCompositor->SetClipping(aClipRect);
  }
}

void
EmbedLiteViewThreadParent::SetTransformation(float aScale, nsIntPoint aScrollOffset)
{
  if (mCompositor) {
    mCompositor->SetTransformation(aScale, aScrollOffset);
  }
}

void
EmbedLiteViewThreadParent::ScheduleRender()
{
  if (mCompositor) {
    mCompositor->ScheduleRenderOnCompositorThread();
  }
}

void
EmbedLiteViewThreadParent::ReceiveInputEvent(const InputData& aEvent)
{
  if (mController) {
    mController->ReceiveInputEvent(aEvent);
    if (aEvent.mInputType == MULTITOUCH_INPUT) {
      const MultiTouchInput& multiTouchInput = aEvent.AsMultiTouchInput();
      gfxSize sz = mController->CalculateResolution();
      if (multiTouchInput.mType == MultiTouchInput::MULTITOUCH_START ||
          multiTouchInput.mType == MultiTouchInput::MULTITOUCH_ENTER) {
        mInTouchProcess = true;
      } else if (multiTouchInput.mType == MultiTouchInput::MULTITOUCH_END ||
                 multiTouchInput.mType == MultiTouchInput::MULTITOUCH_LEAVE) {
        mInTouchProcess = false;
      }
      gfxPoint diff = mController->GetTempScrollOffset();
      if (multiTouchInput.mType == MultiTouchInput::MULTITOUCH_MOVE) {
        unused << SendInputDataTouchMoveEvent(multiTouchInput, sz, diff);
      } else {
        unused << SendInputDataTouchEvent(multiTouchInput, sz, diff);
      }
    }
  }
}

void
EmbedLiteViewThreadParent::TextEvent(const char* composite, const char* preEdit)
{
  LOGT("commit:%s, pre:%s, mLastIMEState:%i", composite, preEdit, mLastIMEState);
  if (mLastIMEState) {
    unused << SendHandleTextEvent(NS_ConvertUTF8toUTF16(nsDependentCString(composite)),
                                  NS_ConvertUTF8toUTF16(nsDependentCString(preEdit)));
  } else {
    NS_ERROR("Text event must not be sent while IME disabled");
  }
}

void
EmbedLiteViewThreadParent::ViewAPIDestroyed()
{
  mViewAPIDestroyed = true;
  mView = nullptr;
}

void
EmbedLiteViewThreadParent::SendKeyPress(int domKeyCode, int gmodifiers, int charCode)
{
  LOGT("dom:%i, mod:%i, char:'%c'", domKeyCode, gmodifiers, charCode);
  unused << SendHandleKeyPressEvent(domKeyCode, gmodifiers, charCode);
}

void
EmbedLiteViewThreadParent::SendKeyRelease(int domKeyCode, int gmodifiers, int charCode)
{
  LOGT("dom:%i, mod:%i, char:'%c'", domKeyCode, gmodifiers, charCode);
  unused << SendHandleKeyReleaseEvent(domKeyCode, gmodifiers, charCode);
}

void
EmbedLiteViewThreadParent::MousePress(int x, int y, int mstime, unsigned int buttons, unsigned int modifiers)
{
  LOGT("pt[%i,%i], t:%i, bt:%u, mod:%u", x, y, mstime, buttons, modifiers);
  if (mController) {
    MultiTouchInput event(MultiTouchInput::MULTITOUCH_START, mstime);
    event.mTouches.AppendElement(SingleTouchData(0,
                                                 nsIntPoint(x, y),
                                                 nsIntPoint(1, 1),
                                                 180.0f,
                                                 1.0f));
    mController->ReceiveInputEvent(event);
    unused << SendMouseEvent(NS_LITERAL_STRING("mousedown"),
                             x, y, buttons, 1, modifiers,
                             true);
  }
}

void
EmbedLiteViewThreadParent::MouseRelease(int x, int y, int mstime, unsigned int buttons, unsigned int modifiers)
{
  LOGT("pt[%i,%i], t:%i, bt:%u, mod:%u", x, y, mstime, buttons, modifiers);
  if (mController) {
    MultiTouchInput event(MultiTouchInput::MULTITOUCH_END, mstime);
    event.mTouches.AppendElement(SingleTouchData(0,
                                                 nsIntPoint(x, y),
                                                 nsIntPoint(1, 1),
                                                 180.0f,
                                                 1.0f));
    mController->ReceiveInputEvent(event);
    unused << SendMouseEvent(NS_LITERAL_STRING("mouseup"),
                             x, y, buttons, 1, modifiers,
                             true);
  }
}

void
EmbedLiteViewThreadParent::MouseMove(int x, int y, int mstime, unsigned int buttons, unsigned int modifiers)
{
  LOGT("pt[%i,%i], t:%i, bt:%u, mod:%u", x, y, mstime, buttons, modifiers);
  if (mController) {
    MultiTouchInput event(MultiTouchInput::MULTITOUCH_MOVE, mstime);
    event.mTouches.AppendElement(SingleTouchData(0,
                                                 nsIntPoint(x, y),
                                                 nsIntPoint(1, 1),
                                                 180.0f,
                                                 1.0f));
    mController->ReceiveInputEvent(event);
    unused << SendMouseEvent(NS_LITERAL_STRING("mousemove"),
                             x, y, buttons, 1, modifiers,
                             true);
  }
}

bool
EmbedLiteViewThreadParent::RecvGetInputContext(int32_t* aIMEEnabled,
                                               int32_t* aIMEOpen,
                                               intptr_t* aNativeIMEContext)
{
  LOGT("mLastIMEState:%i", mLastIMEState);
  *aIMEEnabled = mLastIMEState;
  *aIMEOpen = IMEState::OPEN_STATE_NOT_SUPPORTED;
  *aNativeIMEContext = 0;
  return true;
}

bool
EmbedLiteViewThreadParent::RecvSetInputContext(const int32_t& aIMEEnabled,
                                               const int32_t& aIMEOpen,
                                               const nsString& aType,
                                               const nsString& aInputmode,
                                               const nsString& aActionHint,
                                               const int32_t& aCause,
                                               const int32_t& aFocusChange)
{
  LOGT("IMEEnabled:%i, IMEOpen:%i, type:%s, imMode:%s, actHint:%s, cause:%i, focusChange:%i, mLastIMEState:%i->%i",
       aIMEEnabled, aIMEOpen, NS_ConvertUTF16toUTF8(aType).get(), NS_ConvertUTF16toUTF8(aInputmode).get(),
       NS_ConvertUTF16toUTF8(aActionHint).get(), aCause, aFocusChange, mLastIMEState, aIMEEnabled);
  if (mViewAPIDestroyed) {
    return true;
  }

  NS_ENSURE_TRUE(mView, false);
  mLastIMEState = aIMEEnabled;
  mView->GetListener()->IMENotification(aIMEEnabled, aIMEOpen, aCause, aFocusChange);
  return true;
}

uint32_t
EmbedLiteViewThreadParent::GetUniqueID()
{
  return mId;
}

} // namespace embedlite
} // namespace mozilla
