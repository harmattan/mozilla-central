/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsXPCOMGlue.h"
#include "EmbedLiteAppService.h"

#include "nsNetCID.h"
#include "nsServiceManagerUtils.h"
#include "nsIObserverService.h"
#include "nsStringGlue.h"
#include "nsIChannel.h"
#include "nsContentUtils.h"

#include "nsIComponentRegistrar.h"
#include "nsIComponentManager.h"
#include "mozilla/GenericFactory.h"
#include "mozilla/ModuleUtils.h"
#include "nsIDocShellTreeItem.h"
#include "nsIDOMWindowUtils.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsComponentManagerUtils.h"
#include "EmbedLiteAppThreadChild.h"
#include "EmbedLiteViewThreadChild.h"
#include "nsIJSContextStack.h"
#include "nsIBaseWindow.h"
#include "nsIWebBrowser.h"
#include "mozilla/layers/AsyncPanZoomController.h"

using namespace mozilla;
using namespace mozilla::embedlite;

EmbedLiteAppService::EmbedLiteAppService()
  : mPushedSomething(0)
{
  mMessageListeners.Init();
}

EmbedLiteAppService::~EmbedLiteAppService()
{
}

NS_IMPL_ISUPPORTS2(EmbedLiteAppService, nsIObserver, nsIEmbedAppService)

NS_IMETHODIMP
EmbedLiteAppService::Observe(nsISupports* aSubject,
                             const char* aTopic,
                             const PRUnichar* aData)
{
  return NS_OK;
}

static EmbedLiteViewThreadChild* sGetViewById(uint32_t aId)
{
  EmbedLiteAppThreadChild* app = EmbedLiteAppThreadChild::GetInstance();
  NS_ENSURE_TRUE(app, nullptr);
  EmbedLiteViewThreadChild* view = app->GetViewByID(aId);
  return view;
}

void EmbedLiteAppService::RegisterView(uint32_t aId)
{
  EmbedLiteViewThreadChild* view = sGetViewById(aId);
  NS_ENSURE_TRUE(view, );
  mIDMap[view->GetOuterID()] = aId;
}

void EmbedLiteAppService::UnregisterView(uint32_t aId)
{
  std::map<uint64_t, uint32_t>::iterator it;
  for (it = mIDMap.begin(); it != mIDMap.end(); ++it) {
    if (aId == it->second) {
      break;
    }
  }
  mIDMap.erase(it);
}

NS_IMETHODIMP
EmbedLiteAppService::GetIDByWindow(nsIDOMWindow* aWin, uint32_t* aId)
{
  nsCxPusher pusher;
  pusher.PushNull();
  nsCOMPtr<nsIDOMWindow> window;
  nsCOMPtr<nsIWebNavigation> navNav(do_GetInterface(aWin));
  nsCOMPtr<nsIDocShellTreeItem> navItem(do_QueryInterface(navNav));
  NS_ENSURE_TRUE(navItem, NS_ERROR_FAILURE);
  nsCOMPtr<nsIDocShellTreeItem> rootItem;
  navItem->GetRootTreeItem(getter_AddRefs(rootItem));
  nsCOMPtr<nsIDOMWindow> rootWin(do_GetInterface(rootItem));
  NS_ENSURE_TRUE(rootWin, NS_ERROR_FAILURE);
  rootWin->GetTop(getter_AddRefs(window));
  nsCOMPtr<nsIDOMWindowUtils> utils = do_GetInterface(window);
  uint64_t OuterWindowID = 0;
  utils->GetOuterWindowID(&OuterWindowID);
  *aId = mIDMap[OuterWindowID];
  return NS_OK;
}

NS_IMETHODIMP
EmbedLiteAppService::SendAsyncMessage(uint32_t aId, const PRUnichar* messageName, const PRUnichar* message)
{
  EmbedLiteViewThreadChild* view = sGetViewById(aId);
  NS_ENSURE_TRUE(view, NS_ERROR_FAILURE);
  view->DoSendAsyncMessage(messageName, message);
  return NS_OK;
}

NS_IMETHODIMP
EmbedLiteAppService::SendSyncMessage(uint32_t aId, const PRUnichar* messageName, const PRUnichar* message, nsAString& retval)
{
  EmbedLiteViewThreadChild* view = sGetViewById(aId);
  NS_ENSURE_TRUE(view, NS_ERROR_FAILURE);
  InfallibleTArray<nsString> retvalArray;
  view->DoSendSyncMessage(messageName, message, &retvalArray);
  retval = retvalArray[0];
  return NS_OK;
}

NS_IMETHODIMP
EmbedLiteAppService::AddMessageListener(const char* name, nsIEmbedMessageListener* listener)
{
  nsTArray<nsCOMPtr<nsIEmbedMessageListener> >* array;
  nsDependentCString cstrname(name);
  if (!mMessageListeners.Get(cstrname, &array)) {
    array = new nsTArray<nsCOMPtr<nsIEmbedMessageListener> >();
    mMessageListeners.Put(cstrname, array);
  }

  array->AppendElement(listener);

  return NS_OK;
}

NS_IMETHODIMP
EmbedLiteAppService::RemoveMessageListener(const char* name, nsIEmbedMessageListener* aListener)
{
  nsTArray<nsCOMPtr<nsIEmbedMessageListener> >* array;
  nsDependentCString cstrname(name);
  if (!mMessageListeners.Get(cstrname, &array)) {
    return NS_ERROR_FAILURE;
  }

  for (uint32_t i = 0; i < array->Length(); i++) {
    nsCOMPtr<nsIEmbedMessageListener> listener = array->ElementAt(i);
    if (listener == aListener) {
      array->RemoveElementAt(i);
      if (array->IsEmpty()) {
        mMessageListeners.Remove(cstrname);
      }
      break;
    }
  }

  return NS_OK;
}

void
EmbedLiteAppService::HandleAsyncMessage(const char* aMessage, const nsString& aData)
{
  nsTArray<nsCOMPtr<nsIEmbedMessageListener> >* array;
  if (!mMessageListeners.Get(nsDependentCString(aMessage), &array)) {
    return;
  }

  for (uint32_t i = 0; i < array->Length(); i++) {
    nsCOMPtr<nsIEmbedMessageListener>& listener = array->ElementAt(i);
    listener->OnMessageReceived(aMessage, aData.get());
  }
}

NS_IMETHODIMP EmbedLiteAppService::EnterSecureJSContext()
{
  nsIThreadJSContextStack* stack = nsContentUtils::ThreadJSContextStack();
  if (!stack) {
    return NS_OK;
  }

  if (NS_FAILED(stack->Push(nullptr))) {
    return NS_ERROR_FAILURE;
  }

  mPushedSomething++;
  return NS_OK;
}

NS_IMETHODIMP EmbedLiteAppService::LeaveSecureJSContext()
{
  nsIThreadJSContextStack* stack = nsContentUtils::ThreadJSContextStack();
  if (!mPushedSomething || !stack) {
    mPushedSomething = 0;
    return NS_ERROR_FAILURE;
  }

  JSContext* unused;
  stack->Pop(&unused);

  mPushedSomething--;
  return NS_OK;
}

JSContext*
EmbedLiteAppService::GetAnyJSContext(uint32_t aWinId)
{
  EmbedLiteViewThreadChild* view = sGetViewById(aWinId);
  NS_ENSURE_TRUE(view, nullptr);
  return view->GetJSContext();
}

NS_IMETHODIMP
EmbedLiteAppService::AddContentListener(uint32_t aWinId, mozilla::layers::GeckoContentController* listener)
{
  EmbedLiteViewThreadChild* view = sGetViewById(aWinId);
  NS_ENSURE_TRUE(view, NS_ERROR_FAILURE);
  view->AddGeckoContentListener(listener);
  return NS_OK;
}

NS_IMETHODIMP
EmbedLiteAppService::RemoveContentListener(uint32_t aWinId, mozilla::layers::GeckoContentController* listener)
{
  EmbedLiteViewThreadChild* view = sGetViewById(aWinId);
  NS_ENSURE_TRUE(view, NS_ERROR_FAILURE);
  view->RemoveGeckoContentListener(listener);
  return NS_OK;
}

NS_IMETHODIMP
EmbedLiteAppService::ZoomToRect(uint32_t aWinId, float aX, float aY, float aWidth, float aHeight)
{
  EmbedLiteViewThreadChild* view = sGetViewById(aWinId);
  NS_ENSURE_TRUE(view, NS_ERROR_FAILURE);
  view->SendZoomToRect(gfxRect(aX, aY, aWidth, aHeight));
  return NS_OK;
}

NS_IMETHODIMP
EmbedLiteAppService::ContentReceivedTouch(uint32_t aWinId, bool aPreventDefault)
{
  EmbedLiteViewThreadChild* view = sGetViewById(aWinId);
  NS_ENSURE_TRUE(view, NS_ERROR_FAILURE);
  view->SendContentReceivedTouch(aPreventDefault);
  return NS_OK;
}

NS_IMETHODIMP
EmbedLiteAppService::DetectScrollableSubframe(uint32_t aWinId)
{
  EmbedLiteViewThreadChild* view = sGetViewById(aWinId);
  NS_ENSURE_TRUE(view, NS_ERROR_FAILURE);
  view->SendDetectScrollableSubframe();
  return NS_OK;
}

NS_IMETHODIMP
EmbedLiteAppService::CancelDefaultPanZoom(uint32_t aWinId)
{
  EmbedLiteViewThreadChild* view = sGetViewById(aWinId);
  NS_ENSURE_TRUE(view, NS_ERROR_FAILURE);
  view->SendCancelDefaultPanZoom();
  return NS_OK;
}

NS_IMETHODIMP
EmbedLiteAppService::GetBrowserByID(uint32_t aId, nsIWebBrowser * *outWindow)
{
  EmbedLiteViewThreadChild* view = sGetViewById(aId);
  NS_ENSURE_TRUE(view, NS_ERROR_FAILURE);
  nsresult rv;
  nsCOMPtr<nsIWebBrowser> br;
  view->GetBrowser(getter_AddRefs(br));
  *outWindow = br.forget().get();
  return rv;
}

NS_IMETHODIMP
EmbedLiteAppService::GetCompositedRectInCSS(const mozilla::layers::FrameMetrics& aFrameMetrics,
                                            float* aX, float* aY, float* aWidth, float* aHeight)
{
  mozilla::gfx::Rect cssCompositedRect =
    mozilla::layers::AsyncPanZoomController::CalculateCompositedRectInCssPixels(aFrameMetrics);
  *aX = cssCompositedRect.x;
  *aY = cssCompositedRect.y;
  *aWidth = cssCompositedRect.width;
  *aHeight = cssCompositedRect.height;

  return NS_OK;
}
