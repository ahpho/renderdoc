/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2015-2026 Baldur Karlsson
 * Copyright (c) 2014 Crytek
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "core/core.h"
#include "hooks/hooks.h"
#include "dxgi_wrapped.h"

typedef HRESULT(WINAPI *PFN_CREATE_DXGI_FACTORY)(REFIID, void **);
typedef HRESULT(WINAPI *PFN_CREATE_DXGI_FACTORY2)(UINT, REFIID, void **);
typedef HRESULT(WINAPI *PFN_GET_DEBUG_INTERFACE)(REFIID, void **);
typedef HRESULT(WINAPI *PFN_GET_DEBUG_INTERFACE1)(UINT, REFIID, void **);

MIDL_INTERFACE("9F251514-9D4D-4902-9D60-18988AB7D4B5")
IDXGraphicsAnalysis : public IUnknown
{
  virtual void STDMETHODCALLTYPE BeginCapture() = 0;
  virtual void STDMETHODCALLTYPE EndCapture() = 0;
};

struct RenderDocAnalysis : IDXGraphicsAnalysis
{
  // IUnknown boilerplate
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) { return E_NOINTERFACE; }
  ULONG STDMETHODCALLTYPE AddRef()
  {
    InterlockedIncrement(&m_iRefcount);
    return m_iRefcount;
  }
  ULONG STDMETHODCALLTYPE Release() { return InterlockedDecrement(&m_iRefcount); }
  unsigned int m_iRefcount = 0;

  // IDXGraphicsAnalysis
  void STDMETHODCALLTYPE BeginCapture()
  {
    DeviceOwnedWindow devWnd;
    RenderDoc::Inst().GetActiveWindow(devWnd);

    RenderDoc::Inst().StartFrameCapture(devWnd);
  }

  void STDMETHODCALLTYPE EndCapture()
  {
    DeviceOwnedWindow devWnd;
    RenderDoc::Inst().GetActiveWindow(devWnd);

    RenderDoc::Inst().EndFrameCapture(devWnd);
  }
};

struct DummyDXGIInfoQueue : public IDXGIInfoQueue
{
public:
  // IUnknown boilerplate
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) { return E_NOINTERFACE; }
  ULONG STDMETHODCALLTYPE AddRef()
  {
    InterlockedIncrement(&m_iRefcount);
    return m_iRefcount;
  }
  ULONG STDMETHODCALLTYPE Release() { return InterlockedDecrement(&m_iRefcount); }
  unsigned int m_iRefcount = 0;
  // IDXGIInfoQueue
  virtual HRESULT STDMETHODCALLTYPE SetMessageCountLimit(DXGI_DEBUG_ID Producer,
                                                         UINT64 MessageCountLimit)
  {
    return S_OK;
  }

  virtual void STDMETHODCALLTYPE ClearStoredMessages(DXGI_DEBUG_ID Producer) { return; }
  virtual HRESULT STDMETHODCALLTYPE GetMessage(DXGI_DEBUG_ID Producer, UINT64 MessageIndex,
                                               _Out_writes_bytes_opt_(*pMessageByteLength)
                                                   DXGI_INFO_QUEUE_MESSAGE *pMessage,
                                               _Inout_ SIZE_T *pMessageByteLength)
  {
    return S_OK;
  }

  virtual UINT64 STDMETHODCALLTYPE GetNumStoredMessagesAllowedByRetrievalFilters(DXGI_DEBUG_ID Producer)
  {
    return 0;
  }

  virtual UINT64 STDMETHODCALLTYPE GetNumStoredMessages(DXGI_DEBUG_ID Producer) { return 0; }
  virtual UINT64 STDMETHODCALLTYPE GetNumMessagesDiscardedByMessageCountLimit(DXGI_DEBUG_ID Producer)
  {
    return 0;
  }

  virtual UINT64 STDMETHODCALLTYPE GetMessageCountLimit(DXGI_DEBUG_ID Producer) { return 0; }
  virtual UINT64 STDMETHODCALLTYPE GetNumMessagesAllowedByStorageFilter(DXGI_DEBUG_ID Producer)
  {
    return 0;
  }

  virtual UINT64 STDMETHODCALLTYPE GetNumMessagesDeniedByStorageFilter(DXGI_DEBUG_ID Producer)
  {
    return 0;
  }

  virtual HRESULT STDMETHODCALLTYPE AddStorageFilterEntries(DXGI_DEBUG_ID Producer,
                                                            DXGI_INFO_QUEUE_FILTER *pFilter)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE GetStorageFilter(DXGI_DEBUG_ID Producer,
                                                     _Out_writes_bytes_opt_(*pFilterByteLength)
                                                         DXGI_INFO_QUEUE_FILTER *pFilter,
                                                     _Inout_ SIZE_T *pFilterByteLength)
  {
    return S_OK;
  }

  virtual void STDMETHODCALLTYPE ClearStorageFilter(DXGI_DEBUG_ID Producer) { return; }
  virtual HRESULT STDMETHODCALLTYPE PushEmptyStorageFilter(DXGI_DEBUG_ID Producer) { return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE PushDenyAllStorageFilter(DXGI_DEBUG_ID Producer)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE PushCopyOfStorageFilter(DXGI_DEBUG_ID Producer) { return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE PushStorageFilter(DXGI_DEBUG_ID Producer,
                                                      DXGI_INFO_QUEUE_FILTER *pFilter)
  {
    return S_OK;
  }

  virtual void STDMETHODCALLTYPE PopStorageFilter(DXGI_DEBUG_ID Producer) { return; }
  virtual UINT STDMETHODCALLTYPE GetStorageFilterStackSize(DXGI_DEBUG_ID Producer) { return 0; }
  virtual HRESULT STDMETHODCALLTYPE AddRetrievalFilterEntries(DXGI_DEBUG_ID Producer,
                                                              DXGI_INFO_QUEUE_FILTER *pFilter)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE GetRetrievalFilter(DXGI_DEBUG_ID Producer,
                                                       _Out_writes_bytes_opt_(*pFilterByteLength)
                                                           DXGI_INFO_QUEUE_FILTER *pFilter,
                                                       _Inout_ SIZE_T *pFilterByteLength)
  {
    return S_OK;
  }

  virtual void STDMETHODCALLTYPE ClearRetrievalFilter(DXGI_DEBUG_ID Producer) { return; }
  virtual HRESULT STDMETHODCALLTYPE PushEmptyRetrievalFilter(DXGI_DEBUG_ID Producer)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE PushDenyAllRetrievalFilter(DXGI_DEBUG_ID Producer)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE PushCopyOfRetrievalFilter(DXGI_DEBUG_ID Producer)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE PushRetrievalFilter(DXGI_DEBUG_ID Producer,
                                                        DXGI_INFO_QUEUE_FILTER *pFilter)
  {
    return S_OK;
  }

  virtual void STDMETHODCALLTYPE PopRetrievalFilter(DXGI_DEBUG_ID Producer) { return; }
  virtual UINT STDMETHODCALLTYPE GetRetrievalFilterStackSize(DXGI_DEBUG_ID Producer) { return 0; }
  virtual HRESULT STDMETHODCALLTYPE AddMessage(DXGI_DEBUG_ID Producer,
                                               DXGI_INFO_QUEUE_MESSAGE_CATEGORY Category,
                                               DXGI_INFO_QUEUE_MESSAGE_SEVERITY Severity,
                                               DXGI_INFO_QUEUE_MESSAGE_ID ID, LPCSTR pDescription)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE AddApplicationMessage(DXGI_INFO_QUEUE_MESSAGE_SEVERITY Severity,
                                                          LPCSTR pDescription)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE SetBreakOnCategory(DXGI_DEBUG_ID Producer,
                                                       DXGI_INFO_QUEUE_MESSAGE_CATEGORY Category,
                                                       BOOL bEnable)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE SetBreakOnSeverity(DXGI_DEBUG_ID Producer,
                                                       DXGI_INFO_QUEUE_MESSAGE_SEVERITY Severity,
                                                       BOOL bEnable)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE SetBreakOnID(DXGI_DEBUG_ID Producer,
                                                 DXGI_INFO_QUEUE_MESSAGE_ID ID, BOOL bEnable)
  {
    return S_OK;
  }

  virtual BOOL STDMETHODCALLTYPE GetBreakOnCategory(DXGI_DEBUG_ID Producer,
                                                    DXGI_INFO_QUEUE_MESSAGE_CATEGORY Category)
  {
    return FALSE;
  }

  virtual BOOL STDMETHODCALLTYPE GetBreakOnSeverity(DXGI_DEBUG_ID Producer,
                                                    DXGI_INFO_QUEUE_MESSAGE_SEVERITY Severity)
  {
    return FALSE;
  }

  virtual BOOL STDMETHODCALLTYPE GetBreakOnID(DXGI_DEBUG_ID Producer, DXGI_INFO_QUEUE_MESSAGE_ID ID)
  {
    return FALSE;
  }

  virtual void STDMETHODCALLTYPE SetMuteDebugOutput(DXGI_DEBUG_ID Producer, BOOL bMute) { return; }
  virtual BOOL STDMETHODCALLTYPE GetMuteDebugOutput(DXGI_DEBUG_ID Producer) { return FALSE; }
};

class DXGIHook : LibraryHook
{
public:
  DXGIHook() { m_nameLibraryHook = "DXGIHook"; }

public:
  void RegisterHooks()
  {
    RDCLOG("Registering DXGI hooks");

    LibraryHooks::RegisterLibraryHook("dxgi.dll", NULL);

    m_RecurseSlot = Threading::AllocateTLSSlot();
    Threading::SetTLSValue(m_RecurseSlot, NULL);

    CreateDXGIFactory.Register("dxgi.dll", "CreateDXGIFactory", CreateDXGIFactory_hook);
    CreateDXGIFactory1.Register("dxgi.dll", "CreateDXGIFactory1", CreateDXGIFactory1_hook);
    CreateDXGIFactory2.Register("dxgi.dll", "CreateDXGIFactory2", CreateDXGIFactory2_hook);
    GetDebugInterface.Register("dxgi.dll", "DXGIGetDebugInterface", DXGIGetDebugInterface_hook);
    GetDebugInterface1.Register("dxgi.dll", "DXGIGetDebugInterface1", DXGIGetDebugInterface1_hook);

    // ksh: also hook the NVIDIA Streamline interposer's re-exported factory entry points, since the
    // game calls those instead of dxgi.dll. Separate HookedFunction objects; the re-entrancy guard
    // in CreateDXGIFactory*_Impl prevents double-wrapping when Streamline internally calls real dxgi.
    RDCLOG("Registering sl.interposer.dll DXGI factory hooks (NVIDIA Streamline)");
    LibraryHooks::RegisterLibraryHook("sl.interposer.dll", NULL);
    CreateDXGIFactory_Interposer.Register("sl.interposer.dll", "CreateDXGIFactory",
                                          CreateDXGIFactory_Interposer_hook);
    CreateDXGIFactory1_Interposer.Register("sl.interposer.dll", "CreateDXGIFactory1",
                                           CreateDXGIFactory1_Interposer_hook);
    CreateDXGIFactory2_Interposer.Register("sl.interposer.dll", "CreateDXGIFactory2",
                                           CreateDXGIFactory2_Interposer_hook);
  }

private:
  static DXGIHook dxgihooks;

  RenderDocAnalysis m_RenderDocAnalysis;
  DummyDXGIInfoQueue m_DummyInfoQueue;

  HookedFunction<PFN_CREATE_DXGI_FACTORY> CreateDXGIFactory;
  HookedFunction<PFN_CREATE_DXGI_FACTORY> CreateDXGIFactory1;
  HookedFunction<PFN_CREATE_DXGI_FACTORY2> CreateDXGIFactory2;
  HookedFunction<PFN_GET_DEBUG_INTERFACE> GetDebugInterface;
  HookedFunction<PFN_GET_DEBUG_INTERFACE1> GetDebugInterface1;

  // ksh: NVIDIA Streamline interposer (sl.interposer.dll) re-exports CreateDXGIFactory*. The game
  // links against those, not dxgi.dll, so we must hook the interposer to see the app's factory
  // (and therefore swapchain) creation. Separate HookedFunction objects (own orig) per the
  // shim-recursion lesson from d3d12_hooks.cpp.
  HookedFunction<PFN_CREATE_DXGI_FACTORY> CreateDXGIFactory_Interposer;
  HookedFunction<PFN_CREATE_DXGI_FACTORY> CreateDXGIFactory1_Interposer;
  HookedFunction<PFN_CREATE_DXGI_FACTORY2> CreateDXGIFactory2_Interposer;

  // ksh: re-entrancy guard. When our interposer factory hook runs, Streamline internally calls the
  // real dxgi.dll!CreateDXGIFactory, which our GetProcAddress hook redirects back to our dxgi.dll
  // factory hook -> that would wrap the factory a second time. The guard makes the inner call pass
  // through unwrapped so only the outer (interposer) layer wraps.
  static uint64_t m_RecurseSlot;
  static bool CheckRecurse()
  {
    if(Threading::GetTLSValue(m_RecurseSlot) == NULL)
    {
      Threading::SetTLSValue(m_RecurseSlot, (void *)1);
      return false;
    }
    return true;
  }
  static void EndRecurse() { Threading::SetTLSValue(m_RecurseSlot, NULL); }

  // shared body for CreateDXGIFactory / CreateDXGIFactory1 (same signature). The real function is
  // passed explicitly to avoid shim recursion between dxgi.dll and sl.interposer.dll.
  static HRESULT CreateDXGIFactory_Impl(PFN_CREATE_DXGI_FACTORY realFunc, const char *tag,
                                        const char *apiName, REFIID riid, void **ppFactory)
  {
    if(ppFactory)
      *ppFactory = NULL;

    if(!realFunc)
    {
      RDCERR("[%s:%s] no real function pointer!", apiName, tag);
      return E_UNEXPECTED;
    }

    bool recurse = CheckRecurse();

    HRESULT ret = realFunc(riid, ppFactory);
    RDCLOG("[%s:%s] -------- ret=0x%x, recurse=%d --------", apiName, tag, ret, (int)recurse);

    // only wrap at the outermost layer; if we're re-entered (Streamline calling the real dxgi),
    // pass the real factory straight through.
    if(SUCCEEDED(ret) && !recurse)
      RefCountDXGIObject::HandleWrap(apiName, riid, ppFactory);

    if(!recurse)
      EndRecurse();

    return ret;
  }

  static HRESULT CreateDXGIFactory2_Impl(PFN_CREATE_DXGI_FACTORY2 realFunc, const char *tag,
                                         UINT Flags, REFIID riid, void **ppFactory)
  {
    if(ppFactory)
      *ppFactory = NULL;

    if(!realFunc)
    {
      RDCERR("[CreateDXGIFactory2:%s] no real function pointer!", tag);
      return E_UNEXPECTED;
    }

    bool recurse = CheckRecurse();

    HRESULT ret = realFunc(Flags, riid, ppFactory);
    RDCLOG("[CreateDXGIFactory2:%s] -------- ret=0x%x, recurse=%d --------", tag, ret,
           (int)recurse);

    if(SUCCEEDED(ret) && !recurse)
      RefCountDXGIObject::HandleWrap("CreateDXGIFactory2", riid, ppFactory);

    if(!recurse)
      EndRecurse();

    return ret;
  }

  static HRESULT WINAPI CreateDXGIFactory_hook(__in REFIID riid, __out void **ppFactory)
  {
    return CreateDXGIFactory_Impl(dxgihooks.CreateDXGIFactory(), "dxgi", "CreateDXGIFactory", riid,
                                  ppFactory);
  }

  static HRESULT WINAPI CreateDXGIFactory_Interposer_hook(__in REFIID riid, __out void **ppFactory)
  {
    return CreateDXGIFactory_Impl(dxgihooks.CreateDXGIFactory_Interposer(), "sl",
                                  "CreateDXGIFactory", riid, ppFactory);
  }

  static HRESULT WINAPI CreateDXGIFactory1_hook(__in REFIID riid, __out void **ppFactory)
  {
    return CreateDXGIFactory_Impl(dxgihooks.CreateDXGIFactory1(), "dxgi", "CreateDXGIFactory1", riid,
                                  ppFactory);
  }

  static HRESULT WINAPI CreateDXGIFactory1_Interposer_hook(__in REFIID riid, __out void **ppFactory)
  {
    return CreateDXGIFactory_Impl(dxgihooks.CreateDXGIFactory1_Interposer(), "sl",
                                  "CreateDXGIFactory1", riid, ppFactory);
  }

  static HRESULT WINAPI CreateDXGIFactory2_hook(UINT Flags, REFIID riid, void **ppFactory)
  {
    return CreateDXGIFactory2_Impl(dxgihooks.CreateDXGIFactory2(), "dxgi", Flags, riid, ppFactory);
  }

  static HRESULT WINAPI CreateDXGIFactory2_Interposer_hook(UINT Flags, REFIID riid, void **ppFactory)
  {
    return CreateDXGIFactory2_Impl(dxgihooks.CreateDXGIFactory2_Interposer(), "sl", Flags, riid,
                                   ppFactory);
  }

  static HRESULT WINAPI DXGIGetDebugInterface_hook(REFIID riid, void **ppDebug)
  {
    if(ppDebug)
      *ppDebug = NULL;

    if(riid == __uuidof(IDXGraphicsAnalysis))
    {
      dxgihooks.m_RenderDocAnalysis.AddRef();
      if(ppDebug)
        *ppDebug = &dxgihooks.m_RenderDocAnalysis;
      RDCLOG("DXGIGetDebugInterface_hook == uuidof(IDXGraphicsAnalysis)");
      return S_OK;
    }
    if(riid == __uuidof(IDXGIInfoQueue))
    {
      RDCWARN(
          "Returning a dummy IDXGIInfoQueue that does nothing. RenderDoc takes control of the "
          "debug layer.");
      RDCLOG("DXGIGetDebugInterface_hook == uuidof(IDXGIInfoQueue)");

      dxgihooks.m_DummyInfoQueue.AddRef();
      if(ppDebug)
        *ppDebug = &dxgihooks.m_DummyInfoQueue;
      return S_OK;
    }

    // IDXGIDebug and IDXGIDebug1 can come through here, but we don't need to wrap them.
    if(dxgihooks.GetDebugInterface())
    {
      RDCLOG("DXGIGetDebugInterface_hook == dxgihooks.GetDebugInterface()");
      return dxgihooks.GetDebugInterface()(riid, ppDebug);
    }
    else
    {
      RDCLOG("error: DXGIGetDebugInterface_hook == E_NOINTERFACE");
      return E_NOINTERFACE;
    }
  }

  static HRESULT WINAPI DXGIGetDebugInterface1_hook(UINT Flags, REFIID riid, void **ppDebug)
  {
    if(ppDebug)
      *ppDebug = NULL;

    if(riid == __uuidof(IDXGraphicsAnalysis))
    {
      dxgihooks.m_RenderDocAnalysis.AddRef();
      if(ppDebug)
        *ppDebug = &dxgihooks.m_RenderDocAnalysis;
      RDCLOG("DXGIGetDebugInterface1_hook == uuidof(IDXGraphicsAnalysis)");
      return S_OK;
    }
    if(riid == __uuidof(IDXGIInfoQueue))
    {
      RDCWARN(
          "Returning a dummy IDXGIInfoQueue that does nothing. RenderDoc takes control of the "
          "debug layer.");

      RDCLOG("DXGIGetDebugInterface1_hook == uuidof(IDXGIInfoQueue)");
      dxgihooks.m_DummyInfoQueue.AddRef();
      if(ppDebug)
        *ppDebug = &dxgihooks.m_DummyInfoQueue;
      return S_OK;
    }

    // IDXGIDebug and IDXGIDebug1 can come through here, but we don't need to wrap them.

    if(dxgihooks.GetDebugInterface1())
    {
      RDCLOG("DXGIGetDebugInterface1_hook == dxgihooks.GetDebugInterface1()");
      return dxgihooks.GetDebugInterface1()(Flags, riid, ppDebug);
    }
    else
    {
      RDCLOG("error: DXGIGetDebugInterface1_hook == E_NOINTERFACE");
      return E_NOINTERFACE;
    }
  }
};

DXGIHook DXGIHook::dxgihooks;
uint64_t DXGIHook::m_RecurseSlot = 0;
