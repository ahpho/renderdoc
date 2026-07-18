/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2016-2026 Baldur Karlsson
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

#include "d3d12_hooks.h"
#include "driver/dxgi/dxgi_wrapped.h"
#include "hooks/hooks.h"
#include "serialise/serialiser.h"
#include "d3d12_command_queue.h"
#include "d3d12_device.h"
#include "d3d12_replay.h"
#include "d3d12_shader_cache.h"

#include "driver/dx/official/D3D11On12On7.h"

typedef HRESULT(WINAPI *PFN_D3D12_ENABLE_EXPERIMENTAL_FEATURES)(UINT NumFeatures, const IID *pIIDs,
                                                                void *pConfigurationStructs,
                                                                UINT *pConfigurationStructSizes);

ID3DDevice *GetD3D12DeviceIfAlloc(IUnknown *dev)
{
  if(WrappedID3D12CommandQueue::IsAlloc(dev))
    return (WrappedID3D12CommandQueue *)dev;

  if(WrappedID3D12Device::IsAlloc(dev))
    return (WrappedID3D12Device *)dev;

  return NULL;
}

class WrappedD3D11On12On7 : public RefCounter12<ID3D11On12On7>
{
public:
  WrappedD3D11On12On7(ID3D11On12On7 *real) : RefCounter12(real) {}
  virtual ~WrappedD3D11On12On7() {}
  //////////////////////////////
  // Implement IUnknown
  ULONG STDMETHODCALLTYPE AddRef() { return RefCounter12::AddRef(); }
  ULONG STDMETHODCALLTYPE Release() { return RefCounter12::Release(); }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject)
  {
    if(riid == __uuidof(IUnknown))
    {
      *ppvObject = (IUnknown *)this;
      AddRef();
      return S_OK;
    }

    return E_NOINTERFACE;
  }

  //////////////////////////////
  // Implement ID3D11On12On7

  // Enables usage similar to D3D11On12CreateDevice.
  void STDMETHODCALLTYPE SetThreadDeviceCreationParams(ID3D12Device *pDevice,
                                                       ID3D12CommandQueue *pGraphicsQueue)
  {
    RDCASSERT(WrappedID3D12Device::IsAlloc(pDevice));
    m_pReal->SetThreadDeviceCreationParams(((WrappedID3D12Device *)pDevice)->GetReal(),
                                           Unwrap(pGraphicsQueue));
  }

  // Enables usage similar to ID3D11On12Device::CreateWrappedResource.
  // Note that the D3D11 resource creation parameters should be similar to the D3D12 resource,
  // or else unexpected/undefined behavior may occur.
  void STDMETHODCALLTYPE SetThreadResourceCreationParams(ID3D12Resource *pResource)
  {
    m_pReal->SetThreadResourceCreationParams(Unwrap(pResource));
  }

  ID3D11On12On7Device *STDMETHODCALLTYPE GetThreadLastCreatedDevice()
  {
    // don't need to wrap/unwrap, it only deals with ID3D11On12On7Resource
    return m_pReal->GetThreadLastCreatedDevice();
  }

  ID3D11On12On7Resource *STDMETHODCALLTYPE GetThreadLastCreatedResource()
  {
    // don't need to wrap/unwrap
    return m_pReal->GetThreadLastCreatedResource();
  }
};

// dummy class to present to the user, while we maintain control
//
// The inheritance is awful for these. See WrappedID3D12DebugDevice for why there are multiple
// parent classes
class WrappedID3D12Debug : public RefCounter12<ID3D12Debug>,
                           public ID3D12Debug6,
                           public ID3D12Debug1,
                           public ID3D12Debug2
{
public:
  WrappedID3D12Debug() : RefCounter12(NULL) {}
  virtual ~WrappedID3D12Debug() {}
  //////////////////////////////
  // Implement IUnknown
  ULONG STDMETHODCALLTYPE AddRef() { return RefCounter12::AddRef(); }
  ULONG STDMETHODCALLTYPE Release() { return RefCounter12::Release(); }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject)
  {
    if(riid == __uuidof(IUnknown))
    {
      *ppvObject = (IUnknown *)(ID3D12Debug *)this;
      AddRef();
      return S_OK;
    }
    if(riid == __uuidof(ID3D12Debug))
    {
      *ppvObject = (ID3D12Debug *)this;
      AddRef();
      return S_OK;
    }
    if(riid == __uuidof(ID3D12Debug1))
    {
      *ppvObject = (ID3D12Debug1 *)this;
      AddRef();
      return S_OK;
    }
    if(riid == __uuidof(ID3D12Debug2))
    {
      *ppvObject = (ID3D12Debug2 *)this;
      AddRef();
      return S_OK;
    }
    if(riid == __uuidof(ID3D12Debug3))
    {
      *ppvObject = (ID3D12Debug3 *)this;
      AddRef();
      return S_OK;
    }
    if(riid == __uuidof(ID3D12Debug4))
    {
      *ppvObject = (ID3D12Debug4 *)this;
      AddRef();
      return S_OK;
    }
    if(riid == __uuidof(ID3D12Debug5))
    {
      *ppvObject = (ID3D12Debug5 *)this;
      AddRef();
      return S_OK;
    }
    if(riid == __uuidof(ID3D12Debug6))
    {
      *ppvObject = (ID3D12Debug6 *)this;
      AddRef();
      return S_OK;
    }

    return E_NOINTERFACE;
  }

  //////////////////////////////
  // Implement ID3D12Debug / ID3D12Debug1
  virtual void STDMETHODCALLTYPE EnableDebugLayer() {}
  //////////////////////////////
  // Implement ID3D12Debug1 / ID3D12Debug3
  virtual void STDMETHODCALLTYPE SetEnableGPUBasedValidation(BOOL Enable) {}
  virtual void STDMETHODCALLTYPE SetEnableSynchronizedCommandQueueValidation(BOOL Enable) {}
  // Implement ID3D12Debug2 / ID3D12Debug3
  virtual void STDMETHODCALLTYPE SetGPUBasedValidationFlags(D3D12_GPU_BASED_VALIDATION_FLAGS Flags)
  {
  }
  //////////////////////////////
  // Implement ID3D12Debug4
  virtual void STDMETHODCALLTYPE DisableDebugLayer(void) {}
  //////////////////////////////
  // Implement ID3D12Debug5
  virtual void STDMETHODCALLTYPE SetEnableAutoName(BOOL Enable) {}
  //////////////////////////////
  // Implement ID3D12Debug6
  virtual void STDMETHODCALLTYPE SetForceLegacyBarrierValidation(BOOL Enable) {}
};

class WrappedID3D12Tools : public RefCounter12<ID3D12Tools2>, public ID3D12Tools2
{
  BOOL m_Instrumentation = FALSE;
  ID3D12Tools1 *m_Tools1 = NULL;
  ID3D12Tools2 *m_Tools2 = NULL;
public:
  WrappedID3D12Tools(ID3D12Tools *tools) : RefCounter12(NULL)
  {
    tools->QueryInterface(__uuidof(ID3D12Tools1), (void **)&m_Tools1);
    tools->QueryInterface(__uuidof(ID3D12Tools2), (void **)&m_Tools2);
  }
  virtual ~WrappedID3D12Tools()
  {
    SAFE_RELEASE(m_Tools1);
    SAFE_RELEASE(m_Tools2);
  }
  //////////////////////////////
  // Implement IUnknown
  ULONG STDMETHODCALLTYPE AddRef() { return RefCounter12::AddRef(); }
  ULONG STDMETHODCALLTYPE Release() { return RefCounter12::Release(); }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject)
  {
    if(riid == __uuidof(IUnknown))
    {
      *ppvObject = (IUnknown *)this;
      AddRef();
      return S_OK;
    }
    if(riid == __uuidof(ID3D12Tools))
    {
      *ppvObject = (ID3D12Tools *)this;
      AddRef();
      return S_OK;
    }
    if(riid == __uuidof(ID3D12Tools1))
    {
      *ppvObject = (ID3D12Tools1 *)this;
      AddRef();
      return S_OK;
    }
    if(riid == __uuidof(ID3D12Tools2))
    {
      *ppvObject = (ID3D12Tools2 *)this;
      AddRef();
      return S_OK;
    }

    return E_NOINTERFACE;
  }

  //////////////////////////////
  // Implement ID3D12Tools
  virtual void STDMETHODCALLTYPE EnableShaderInstrumentation(BOOL bEnable)
  {
    m_Instrumentation = bEnable;
  }

  virtual BOOL STDMETHODCALLTYPE ShaderInstrumentationEnabled(void) { return m_Instrumentation; }

  //////////////////////////////
  // Implement ID3D12Tools1
  virtual HRESULT STDMETHODCALLTYPE ReserveGPUVARangesAtCreate(D3D12_GPU_VIRTUAL_ADDRESS_RANGE *pRanges,
                                                               UINT uiNumRanges)
  {
    if(m_Tools1)
      return m_Tools1->ReserveGPUVARangesAtCreate(pRanges, uiNumRanges);
    return E_NOINTERFACE;
  }

  virtual void STDMETHODCALLTYPE ClearReservedGPUVARangesList(void)
  {
    if(m_Tools1)
      m_Tools1->ClearReservedGPUVARangesList();
  }

  //////////////////////////////
  // Implement ID3D12Tools2
  virtual HRESULT STDMETHODCALLTYPE SetApplicationSpecificDriverState(_In_ IUnknown *pAdapter,
                                                                      _In_opt_ ID3DBlob *pBlob)
  {
    if(m_Tools2)
      return m_Tools2->SetApplicationSpecificDriverState(pAdapter, pBlob);
    return E_NOINTERFACE;
  }
};

class WrappedID3D12DeviceRemovedExtendedData : public RefCounter12<ID3D12DeviceRemovedExtendedData1>,
                                               public ID3D12DeviceRemovedExtendedData1
{
public:
  WrappedID3D12DeviceRemovedExtendedData() : RefCounter12(NULL) {}
  virtual ~WrappedID3D12DeviceRemovedExtendedData() {}
  //////////////////////////////
  // Implement IUnknown
  ULONG STDMETHODCALLTYPE AddRef() { return RefCounter12::AddRef(); }
  ULONG STDMETHODCALLTYPE Release() { return RefCounter12::Release(); }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject)
  {
    if(riid == __uuidof(IUnknown))
    {
      *ppvObject = (IUnknown *)this;
      AddRef();
      return S_OK;
    }
    if(riid == __uuidof(ID3D12DeviceRemovedExtendedData))
    {
      *ppvObject = (ID3D12DeviceRemovedExtendedData *)this;
      AddRef();
      return S_OK;
    }
    if(riid == __uuidof(ID3D12DeviceRemovedExtendedData1))
    {
      *ppvObject = (ID3D12DeviceRemovedExtendedData1 *)this;
      AddRef();
      return S_OK;
    }

    return E_NOINTERFACE;
  }

  //////////////////////////////
  // Implement ID3D12DeviceRemovedExtendedData
  virtual HRESULT STDMETHODCALLTYPE
  GetAutoBreadcrumbsOutput(_Out_ D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT *pOutput)
  {
    return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
  }

  virtual HRESULT STDMETHODCALLTYPE
  GetPageFaultAllocationOutput(_Out_ D3D12_DRED_PAGE_FAULT_OUTPUT *pOutput)
  {
    return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
  }

  //////////////////////////////
  // Implement ID3D12DeviceRemovedExtendedData1
  virtual HRESULT STDMETHODCALLTYPE
  GetAutoBreadcrumbsOutput1(_Out_ D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 *pOutput)
  {
    return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
  }

  virtual HRESULT STDMETHODCALLTYPE
  GetPageFaultAllocationOutput1(_Out_ D3D12_DRED_PAGE_FAULT_OUTPUT1 *pOutput)
  {
    return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
  }
};

class WrappedID3D12DeviceFactory : public RefCounter12<ID3D12DeviceFactory>, public ID3D12DeviceFactory
{
  WrappedID3D12DeviceConfiguration config;
public:
  WrappedID3D12DeviceFactory(ID3D12DeviceFactory *real) : RefCounter12(real), config(real, this) {}

  virtual ~WrappedID3D12DeviceFactory() {}
  //////////////////////////////
  // Implement IUnknown
  ULONG STDMETHODCALLTYPE AddRef() { return RefCounter12::AddRef(); }
  ULONG STDMETHODCALLTYPE Release() { return RefCounter12::Release(); }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject)
  {
    if(riid == __uuidof(IUnknown))
    {
      *ppvObject = (IUnknown *)(ID3D12DeviceFactory *)this;
      AddRef();
      return S_OK;
    }
    if(riid == __uuidof(ID3D12DeviceFactory))
    {
      *ppvObject = (ID3D12DeviceFactory *)this;
      AddRef();
      return S_OK;
    }
    if(riid == __uuidof(ID3D12DeviceConfiguration) && config.IsValid())
    {
      *ppvObject = (ID3D12DeviceConfiguration *)&config;
      AddRef();
      return S_OK;
    }
    if(riid == __uuidof(ID3D12DeviceConfiguration1) && config.IsValid1())
    {
      *ppvObject = (ID3D12DeviceConfiguration1 *)&config;
      AddRef();
      return S_OK;
    }

    return E_NOINTERFACE;
  }

  //////////////////////////////
  // Implement ID3D12DeviceFactory

  virtual HRESULT STDMETHODCALLTYPE InitializeFromGlobalState(void)
  {
    return m_pReal->InitializeFromGlobalState();
  }

  virtual HRESULT STDMETHODCALLTYPE ApplyToGlobalState(void)
  {
    return m_pReal->ApplyToGlobalState();
  }

  virtual HRESULT STDMETHODCALLTYPE SetFlags(D3D12_DEVICE_FACTORY_FLAGS flags)
  {
    return m_pReal->SetFlags(flags);
  }

  virtual D3D12_DEVICE_FACTORY_FLAGS STDMETHODCALLTYPE GetFlags(void)
  {
    return m_pReal->GetFlags();
  }

  virtual HRESULT STDMETHODCALLTYPE GetConfigurationInterface(REFCLSID clsid, REFIID iid,
                                                              _COM_Outptr_ void **ppv);

  virtual HRESULT STDMETHODCALLTYPE
  EnableExperimentalFeatures(UINT NumFeatures, _In_reads_(NumFeatures) const IID *pIIDs,
                             _In_reads_opt_(NumFeatures) void *pConfigurationStructs,
                             _In_reads_opt_(NumFeatures) UINT *pConfigurationStructSizes)
  {
    rdcarray<IID> allowedIIDs;

    // allow enabling unsigned DXIL, and GPU upload heaps on most windows versions
    for(UINT i = 0; i < NumFeatures; i++)
    {
      if(pIIDs[i] == D3D12ExperimentalShaderModels)
        allowedIIDs.push_back(D3D12ExperimentalShaderModels);
      else if(pIIDs[i] == D3D12GPUUploadHeapsOnUnsupportedOS)
        allowedIIDs.push_back(D3D12GPUUploadHeapsOnUnsupportedOS);
    }

    // there's no "partially successful" error code, so we just lie to the application and pretend
    // that any filtered IIDs also succeeded
    if(!allowedIIDs.empty())
      return m_pReal->EnableExperimentalFeatures((UINT)allowedIIDs.size(), allowedIIDs.data(), NULL,
                                                 NULL);

    // header says "The call returns E_NOINTERFACE if an unrecognized feature is passed in or
    // Windows Developer mode is not on." so this is the most appropriate error for if no IIDs are
    // allowed.
    return E_NOINTERFACE;
  }

  virtual HRESULT STDMETHODCALLTYPE CreateDevice(_In_opt_ IUnknown *adapter,
                                                 D3D_FEATURE_LEVEL FeatureLevel, REFIID riid,
                                                 _COM_Outptr_opt_ void **ppvDevice)
  {
    RDCLOG("[WrappedID3D12DeviceFactory::CreateDevice] CALLED: adapter=%p, FeatureLevel=%x, riid=%s, ppvDevice=%p",
           adapter, FeatureLevel, ToStr(riid).c_str(), ppvDevice);

    if(RenderDoc::Inst().GetCaptureOptions().apiValidation)
    {
      D3D12DevConfiguration tmpConfig = {};
      HRESULT hr = m_pReal->GetConfigurationInterface(CLSID_D3D12Debug, __uuidof(ID3D12Debug),
                                                      (void **)&tmpConfig.debug);
      if(SUCCEEDED(hr))
      {
        EnableD3D12DebugLayer(&tmpConfig, NULL);
        SAFE_RELEASE(tmpConfig.debug);
      }
    }

    D3D12DevConfiguration devConfig;
    devConfig.devfactory = this;
    devConfig.devconfig = &config;

    HRESULT ret = CreateD3D12_Internal(
        [this](IUnknown *pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid,
               void **ppDevice) {
          return m_pReal->CreateDevice(pAdapter, MinimumFeatureLevel, riid, ppDevice);
        },
        &devConfig, adapter, FeatureLevel, riid, ppvDevice);

    RDCLOG("[WrappedID3D12DeviceFactory::CreateDevice] ret=%s, ppvDevice=%p",
           ToStr(ret).c_str(), ppvDevice);

    return ret;
  }
};

class WrappedID3D12SDKConfiguration : public RefCounter12<ID3D12SDKConfiguration>,
                                      public ID3D12SDKConfiguration1
{
  ID3D12SDKConfiguration1 *m_pReal1 = NULL;
public:
  WrappedID3D12SDKConfiguration(ID3D12SDKConfiguration *real, ID3D12SDKConfiguration1 *real1)
      : RefCounter12(real)
  {
    if(!real1)
      real->QueryInterface(__uuidof(ID3D12SDKConfiguration1), (void **)&real1);
    m_pReal1 = real1;
  }
  virtual ~WrappedID3D12SDKConfiguration()
  {
    SAFE_RELEASE(m_pReal);
    SAFE_RELEASE(m_pReal1);
  }
  //////////////////////////////
  // Implement IUnknown
  ULONG STDMETHODCALLTYPE AddRef() { return RefCounter12::AddRef(); }
  ULONG STDMETHODCALLTYPE Release() { return RefCounter12::Release(); }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject)
  {
    if(riid == __uuidof(IUnknown))
    {
      *ppvObject = (IUnknown *)this;
      AddRef();
      return S_OK;
    }
    if(riid == __uuidof(ID3D12SDKConfiguration))
    {
      *ppvObject = (ID3D12SDKConfiguration *)this;
      AddRef();
      return S_OK;
    }
    if(riid == __uuidof(ID3D12SDKConfiguration1))
    {
      *ppvObject = (ID3D12SDKConfiguration1 *)this;
      AddRef();
      return S_OK;
    }

    return E_NOINTERFACE;
  }

  //////////////////////////////
  // Implement ID3D12SDKConfiguration
  virtual HRESULT STDMETHODCALLTYPE SetSDKVersion(UINT SDKVersion, _In_z_ LPCSTR SDKPath)
  {
    RDCLOG("[WrappedID3D12SDKConfiguration::SetSDKVersion] CALLED: SDKVersion=%u, SDKPath=%s",
           SDKVersion, SDKPath ? SDKPath : "(null)");

    return m_pReal->SetSDKVersion(SDKVersion, SDKPath);
  }

  //////////////////////////////
  // Implement ID3D12SDKConfiguration1
  virtual HRESULT STDMETHODCALLTYPE CreateDeviceFactory(UINT SDKVersion, _In_ LPCSTR SDKPath,
                                                        REFIID riid, _COM_Outptr_ void **ppvFactory)
  {
    RDCLOG("[WrappedID3D12SDKConfiguration::CreateDeviceFactory] CALLED: SDKVersion=%u, SDKPath=%s, riid=%s",
           SDKVersion, SDKPath ? SDKPath : "(null)", ToStr(riid).c_str());

    if(riid != __uuidof(ID3D12DeviceFactory))
    {
      RDCERR("Unexpected uuid to CreateDeviceFactory: %s", ToStr(riid).c_str());
      return E_NOINTERFACE;
    }

    ID3D12DeviceFactory *realFactory = NULL;
    HRESULT hr = m_pReal1->CreateDeviceFactory(SDKVersion, SDKPath, riid, (void **)&realFactory);
    if(SUCCEEDED(hr))
    {
      RDCASSERT(realFactory);
      *ppvFactory = (ID3D12DeviceFactory *)(new WrappedID3D12DeviceFactory(realFactory));

      RDCLOG("[WrappedID3D12SDKConfiguration::CreateDeviceFactory] returned wrapped factory, ppvFactory=%p",
             ppvFactory ? *ppvFactory : NULL);

      return hr;
    }

    RDCLOG("[WrappedID3D12SDKConfiguration::CreateDeviceFactory] failed: hr=0x%x", hr);

    SAFE_RELEASE(realFactory);
    return hr;
  }

  virtual void STDMETHODCALLTYPE FreeUnusedSDKs(void) { return m_pReal1->FreeUnusedSDKs(); }
};

class D3D12Hook : LibraryHook
{
public:
  D3D12Hook() { m_nameLibraryHook = "D3D12Hook"; }

public:
  void RegisterHooks()
  {
    RDCLOG("Registering D3D12 hooks");

    WrappedIDXGISwapChain4::RegisterD3DDeviceCallback(GetD3D12DeviceIfAlloc);

    // also require d3dcompiler_??.dll
    if(GetD3DCompiler() == NULL)
    {
      RDCERR("Failed to load d3dcompiler_??.dll - not inserting D3D12 hooks.");
      return;
    }

    LibraryHooks::RegisterLibraryHook("d3d12.dll", NULL);

    CreateDevice.Register("d3d12.dll", "D3D12CreateDevice", D3D12CreateDevice_hook);
    GetDebugInterface.Register("d3d12.dll", "D3D12GetDebugInterface", D3D12GetDebugInterface_hook);
    GetInterface.Register("d3d12.dll", "D3D12GetInterface", D3D12GetInterface_hook);
    EnableExperimentalFeatures.Register("d3d12.dll", "D3D12EnableExperimentalFeatures",
                                        D3D12EnableExperimentalFeatures_hook);
    GetD3D11On12On7.Register("d3d11on12.dll", "GetD3D11On12On7Interface",
                             GetD3D11On12On7Interface_hook);


    // Modern D3D12 uses the Agility SDK: d3d12.dll is a thin forwarding shim; the real
    // implementation lives in D3D12Core.dll. Games resolve D3D12GetInterface /
    // D3D12CreateDevice from D3D12Core.dll via GetProcAddress, so hooks registered only
    // on d3d12.dll never fire. Register the same hooks on d3d12core.dll too.

    LibraryHooks::RegisterLibraryHook("d3d12core.dll", NULL);

    CreateDevice_Core.Register("d3d12core.dll", "D3D12CreateDevice", D3D12CreateDevice_Core_hook);
    GetDebugInterface_Core.Register("d3d12core.dll", "D3D12GetDebugInterface",
                                    D3D12GetDebugInterface_Core_hook);
    GetInterface_Core.Register("d3d12core.dll", "D3D12GetInterface", D3D12GetInterface_Core_hook);
    EnableExperimentalFeatures_Core.Register("d3d12core.dll", "D3D12EnableExperimentalFeatures",
                                             D3D12EnableExperimentalFeatures_Core_hook);

    // NVIDIA Streamline interposer: sl.interposer.dll re-exports D3D12 entry points, so
    // the game's IAT points at it (not d3d12core.dll). Register on it directly.

    LibraryHooks::RegisterLibraryHook("sl.interposer.dll", NULL);

    CreateDevice_Interposer.Register("sl.interposer.dll", "D3D12CreateDevice",
                                     D3D12CreateDevice_Interposer_hook);
    GetDebugInterface_Interposer.Register("sl.interposer.dll", "D3D12GetDebugInterface",
                                          D3D12GetDebugInterface_Interposer_hook);
    GetInterface_Interposer.Register("sl.interposer.dll", "D3D12GetInterface",
                                     D3D12GetInterface_Interposer_hook);
    EnableExperimentalFeatures_Interposer.Register("sl.interposer.dll",
                                                   "D3D12EnableExperimentalFeatures",
                                                   D3D12EnableExperimentalFeatures_Interposer_hook);


    m_RecurseSlot = Threading::AllocateTLSSlot();
    Threading::SetTLSValue(m_RecurseSlot, NULL);
  }

  static HRESULT GetWrappedInterface(IUnknown *realUnk, REFIID riid, void **ppvInterface)
  {
    if(riid == __uuidof(ID3D12Debug))
    {
      *ppvInterface = (ID3D12Debug *)(new WrappedID3D12Debug());
      return S_OK;
    }
    else if(riid == __uuidof(ID3D12Debug1))
    {
      *ppvInterface = (ID3D12Debug1 *)(new WrappedID3D12Debug());
      return S_OK;
    }
    else if(riid == __uuidof(ID3D12Debug2))
    {
      *ppvInterface = (ID3D12Debug2 *)(new WrappedID3D12Debug());
      return S_OK;
    }
    else if(riid == __uuidof(ID3D12Debug3))
    {
      *ppvInterface = (ID3D12Debug3 *)(new WrappedID3D12Debug());
      return S_OK;
    }
    else if(riid == __uuidof(ID3D12Debug4))
    {
      *ppvInterface = (ID3D12Debug4 *)(new WrappedID3D12Debug());
      return S_OK;
    }
    else if(riid == __uuidof(ID3D12Debug5))
    {
      *ppvInterface = (ID3D12Debug5 *)(new WrappedID3D12Debug());
      return S_OK;
    }
    else if(riid == __uuidof(ID3D12Debug6))
    {
      *ppvInterface = (ID3D12Debug6 *)(new WrappedID3D12Debug());
      return S_OK;
    }
    else if(riid == __uuidof(ID3D12Tools))
    {
      ID3D12Tools *real = (ID3D12Tools *)realUnk;
      // don't need to addref real here, WrappedID3D12Tools doesn't hold onto it but just uses it for QueryInterface
      *ppvInterface = (ID3D12Tools *)(new WrappedID3D12Tools(real));
      return S_OK;
    }
    else if(riid == __uuidof(ID3D12Tools1))
    {
      ID3D12Tools1 *real = (ID3D12Tools1 *)realUnk;
      // don't need to addref real here, WrappedID3D12Tools doesn't hold onto it but just uses it for QueryInterface
      *ppvInterface = (ID3D12Tools1 *)(new WrappedID3D12Tools(real));
      return S_OK;
    }
    else if(riid == __uuidof(ID3D12Tools2))
    {
      ID3D12Tools2 *real = (ID3D12Tools2 *)realUnk;
      // don't need to addref real here, WrappedID3D12Tools doesn't hold onto it but just uses it for QueryInterface
      *ppvInterface = (ID3D12Tools2 *)(new WrappedID3D12Tools(real));
      return S_OK;
    }
    else if(riid == __uuidof(ID3D12DeviceRemovedExtendedData))
    {
      *ppvInterface =
          (ID3D12DeviceRemovedExtendedData *)(new WrappedID3D12DeviceRemovedExtendedData());
      return S_OK;
    }
    else if(riid == __uuidof(ID3D12DeviceRemovedExtendedData1))
    {
      *ppvInterface =
          (ID3D12DeviceRemovedExtendedData1 *)(new WrappedID3D12DeviceRemovedExtendedData());
      return S_OK;
    }
    else if(riid == __uuidof(ID3D12DeviceRemovedExtendedData2))
    {
      *ppvInterface =
          (ID3D12DeviceRemovedExtendedData2 *)(new WrappedID3D12DeviceRemovedExtendedData());
      return S_OK;
    }
    else if(riid == __uuidof(ID3D12SDKConfiguration))
    {
      ID3D12SDKConfiguration *real = (ID3D12SDKConfiguration *)realUnk;
      if(real)
      {
        // take a reference ourselves, realUnk is a transient pointer and will be released after this function returns
        real->AddRef();
        *ppvInterface = (ID3D12SDKConfiguration *)(new WrappedID3D12SDKConfiguration(real, NULL));
        return S_OK;
      }
    }
    else if(riid == __uuidof(ID3D12SDKConfiguration1))
    {
      ID3D12SDKConfiguration1 *real1 = (ID3D12SDKConfiguration1 *)realUnk;
      if(real1)
      {
        // take a reference ourselves, realUnk is a transient pointer and will be released after this function returns
        real1->AddRef();
        ID3D12SDKConfiguration *real = NULL;
        real1->QueryInterface(__uuidof(ID3D12SDKConfiguration), (void **)&real);
        *ppvInterface = (ID3D12SDKConfiguration1 *)(new WrappedID3D12SDKConfiguration(real, real1));
        return S_OK;
      }
    }

    return E_NOINTERFACE;
  }

private:
  static D3D12Hook d3d12hooks;

  HookedFunction<PFN_D3D12_GET_DEBUG_INTERFACE> GetDebugInterface;
  HookedFunction<PFN_D3D12_GET_INTERFACE> GetInterface;
  HookedFunction<PFN_D3D12_CREATE_DEVICE> CreateDevice;
  HookedFunction<PFN_D3D12_ENABLE_EXPERIMENTAL_FEATURES> EnableExperimentalFeatures;
  HookedFunction<PFNGetD3D11On12On7Interface> GetD3D11On12On7;


  // ksh: SEPARATE HookedFunction objects for d3d12core.dll. These MUST NOT share the orig pointer
  // with the d3d12.dll ones above. d3d12.dll's exports are forwarding shims that call into
  // D3D12Core.dll; if the shared orig gets filled with the d3d12.dll shim address, our
  // d3d12core.dll hook would call the shim, which forwards back into the (hooked) d3d12core.dll
  // export -> infinite recursion -> hang during runtime bootstrap. Giving d3d12core.dll its own
  // HookedFunction means its orig is filled directly from D3D12Core.dll's real (non-forwarding)
  // export.
  HookedFunction<PFN_D3D12_GET_DEBUG_INTERFACE> GetDebugInterface_Core;
  HookedFunction<PFN_D3D12_GET_INTERFACE> GetInterface_Core;
  HookedFunction<PFN_D3D12_CREATE_DEVICE> CreateDevice_Core;
  HookedFunction<PFN_D3D12_ENABLE_EXPERIMENTAL_FEATURES> EnableExperimentalFeatures_Core;

  // ksh: SEPARATE HookedFunction objects for sl.interposer.dll (NVIDIA Streamline). Streamline is a
  // D3D12/DXGI interposer for DLSS/Reflex/Frame-Gen: sl.interposer.dll re-exports the standard
  // D3D12/DXGI entry points (D3D12CreateDevice, D3D12GetInterface, CreateDXGIFactory*, ...) and the
  // game links against THOSE (its IAT points at sl.interposer.dll, not d3d12core.dll/dxgi.dll), so
  // our d3d12.dll/d3d12core.dll/dxgi.dll hooks never see the game's device/swapchain creation.
  // Registering the same hook bodies on sl.interposer.dll lets us intercept the app-facing calls
  // before Streamline. Separate HookedFunction objects (own orig) per the shim-recursion lesson.
  // Double-wrap is prevented by Create_Internal's CheckRecurse(): when our interposer hook enters
  // Create_Internal it sets the TLS flag; Streamline's internal call to the real d3d12core create
  // (redirected to our _Core hook) then hits CheckRecurse()==true and just calls the real fn.
  HookedFunction<PFN_D3D12_GET_INTERFACE> GetInterface_Interposer;
  HookedFunction<PFN_D3D12_CREATE_DEVICE> CreateDevice_Interposer;
  HookedFunction<PFN_D3D12_GET_DEBUG_INTERFACE> GetDebugInterface_Interposer;
  HookedFunction<PFN_D3D12_ENABLE_EXPERIMENTAL_FEATURES> EnableExperimentalFeatures_Interposer;


  // re-entrancy detection (can happen in rare cases with e.g. fraps)
  uint64_t m_RecurseSlot = 0;

  void EndRecurse() { Threading::SetTLSValue(m_RecurseSlot, NULL); }
  bool CheckRecurse()
  {
    if(Threading::GetTLSValue(m_RecurseSlot) == NULL)
    {
      Threading::SetTLSValue(m_RecurseSlot, (void *)1);
      return false;
    }

    return true;
  }

  friend HRESULT CreateD3D12_Internal(RealD3D12CreateFunction real, D3D12DevConfiguration *devConfig,
                                      IUnknown *pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel,
                                      REFIID riid, void **ppDevice);

  HRESULT Create_Internal(RealD3D12CreateFunction real, D3D12DevConfiguration *devConfig,
                          IUnknown *pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid,
                          void **ppDevice)
  {
    // if we're already inside a wrapped create i.e. this function, then DON'T do anything
    // special. Just grab the trampolined function and call it.
    if(CheckRecurse())
    {
      RDCLOG("[D3D12 Create_Internal] Recurse detected, calling real directly");
      return real(pAdapter, MinimumFeatureLevel, riid, ppDevice);
    }

    if(riid != __uuidof(ID3D12Device) && riid != __uuidof(ID3D12Device1) &&
       riid != __uuidof(ID3D12Device2) && riid != __uuidof(ID3D12Device3) &&
       riid != __uuidof(ID3D12Device4) && riid != __uuidof(ID3D12Device5) &&
       riid != __uuidof(ID3D12Device6) && riid != __uuidof(ID3D12Device7) &&
       riid != __uuidof(ID3D12Device8) && riid != __uuidof(ID3D12Device9) &&
       riid != __uuidof(ID3D12Device10) && riid != __uuidof(ID3D12Device11) &&
       riid != __uuidof(ID3D12Device12) && riid != __uuidof(ID3D12Device13) &&
       riid != __uuidof(ID3D12Device14))
    {
      RDCERR("Unsupported UUID %s for D3D12CreateDevice", ToStr(riid).c_str());
      return E_NOINTERFACE;
    }

    static LONG s_D3D12CreateCount = 0;
    LONG createIdx = InterlockedIncrement(&s_D3D12CreateCount);
    RDCDEBUG("[D3D12 Create_Internal #%d] ======> Feature Level=%x, riid=%s, pAdapter=%p",
           createIdx, MinimumFeatureLevel, ToStr(riid).c_str(), pAdapter);

    // we should no longer go through here in the replay application
    RDCASSERT(!RenderDoc::Inst().IsReplayApp());

    bool EnableDebugLayer = false;

    if(RenderDoc::Inst().GetCaptureOptions().apiValidation)
      EnableDebugLayer = EnableD3D12DebugLayer(NULL, GetDebugInterface());

    RDCDEBUG("[D3D12 Create_Internal #%d] Calling real createdevice...", createIdx);

    HRESULT ret = real(pAdapter, MinimumFeatureLevel, riid, ppDevice);

    RDCDEBUG("[D3D12 Create_Internal #%d] Called real createdevice. HRESULT: %s, ppDevice=%p",
           createIdx, ToStr(ret).c_str(), ppDevice ? *ppDevice : NULL);

    if(SUCCEEDED(ret) && ppDevice)
    {
      RDCDEBUG("succeeded and hooking.");

      if(!WrappedID3D12Device::IsAlloc(*ppDevice))
      {
        D3D12InitParams params;
        params.MinimumFeatureLevel = MinimumFeatureLevel;

        ID3D12Device *dev = (ID3D12Device *)*ppDevice;

        if(riid == __uuidof(ID3D12Device1))
        {
          ID3D12Device1 *dev1 = (ID3D12Device1 *)*ppDevice;
          dev = (ID3D12Device *)dev1;
        }
        else if(riid == __uuidof(ID3D12Device2))
        {
          ID3D12Device2 *dev2 = (ID3D12Device2 *)*ppDevice;
          dev = (ID3D12Device *)dev2;
        }
        else if(riid == __uuidof(ID3D12Device3))
        {
          ID3D12Device3 *dev3 = (ID3D12Device3 *)*ppDevice;
          dev = (ID3D12Device *)dev3;
        }
        else if(riid == __uuidof(ID3D12Device4))
        {
          ID3D12Device4 *dev4 = (ID3D12Device4 *)*ppDevice;
          dev = (ID3D12Device *)dev4;
        }
        else if(riid == __uuidof(ID3D12Device5))
        {
          ID3D12Device5 *dev5 = (ID3D12Device5 *)*ppDevice;
          dev = (ID3D12Device *)dev5;
        }
        else if(riid == __uuidof(ID3D12Device6))
        {
          ID3D12Device6 *dev6 = (ID3D12Device6 *)*ppDevice;
          dev = (ID3D12Device *)dev6;
        }
        else if(riid == __uuidof(ID3D12Device7))
        {
          ID3D12Device7 *dev7 = (ID3D12Device7 *)*ppDevice;
          dev = (ID3D12Device *)dev7;
        }
        else if(riid == __uuidof(ID3D12Device8))
        {
          ID3D12Device8 *dev8 = (ID3D12Device8 *)*ppDevice;
          dev = (ID3D12Device *)dev8;
        }
        else if(riid == __uuidof(ID3D12Device9))
        {
          ID3D12Device9 *dev9 = (ID3D12Device9 *)*ppDevice;
          dev = (ID3D12Device *)dev9;
        }
        else if(riid == __uuidof(ID3D12Device10))
        {
          ID3D12Device10 *dev10 = (ID3D12Device10 *)*ppDevice;
          dev = (ID3D12Device *)dev10;
        }
        else if(riid == __uuidof(ID3D12Device11))
        {
          ID3D12Device11 *dev11 = (ID3D12Device11 *)*ppDevice;
          dev = (ID3D12Device *)dev11;
        }
        else if(riid == __uuidof(ID3D12Device12))
        {
          ID3D12Device12 *dev12 = (ID3D12Device12 *)*ppDevice;
          dev = (ID3D12Device *)dev12;
        }
        else if(riid == __uuidof(ID3D12Device13))
        {
          ID3D12Device13 *dev13 = (ID3D12Device13 *)*ppDevice;
          dev = (ID3D12Device *)dev13;
        }
        else if(riid == __uuidof(ID3D12Device14))
        {
          ID3D12Device14 *dev14 = (ID3D12Device14 *)*ppDevice;
          dev = (ID3D12Device *)dev14;
        }

        WrappedID3D12Device *wrap = WrappedID3D12Device::Create(dev, params, EnableDebugLayer);

        if(devConfig)
        {
          D3D12DevConfiguration *cfg = new D3D12DevConfiguration(*devConfig);
          wrap->GetReplay()->SetDevConfiguration(cfg);
        }

        RDCDEBUG("created wrapped device: WrappedID3D12Device");

        *ppDevice = (ID3D12Device *)wrap;

        if(riid == __uuidof(ID3D12Device1))
          *ppDevice = (ID3D12Device1 *)wrap;
        else if(riid == __uuidof(ID3D12Device2))
          *ppDevice = (ID3D12Device2 *)wrap;
        else if(riid == __uuidof(ID3D12Device3))
          *ppDevice = (ID3D12Device3 *)wrap;
        else if(riid == __uuidof(ID3D12Device4))
          *ppDevice = (ID3D12Device4 *)wrap;
        else if(riid == __uuidof(ID3D12Device5))
          *ppDevice = (ID3D12Device5 *)wrap;
        else if(riid == __uuidof(ID3D12Device6))
          *ppDevice = (ID3D12Device6 *)wrap;
        else if(riid == __uuidof(ID3D12Device7))
          *ppDevice = (ID3D12Device7 *)wrap;
        else if(riid == __uuidof(ID3D12Device8))
          *ppDevice = (ID3D12Device8 *)wrap;
        else if(riid == __uuidof(ID3D12Device9))
          *ppDevice = (ID3D12Device9 *)wrap;
        else if(riid == __uuidof(ID3D12Device10))
          *ppDevice = (ID3D12Device10 *)wrap;
        else if(riid == __uuidof(ID3D12Device11))
          *ppDevice = (ID3D12Device11 *)wrap;
        else if(riid == __uuidof(ID3D12Device12))
          *ppDevice = (ID3D12Device12 *)wrap;
        else if(riid == __uuidof(ID3D12Device13))
          *ppDevice = (ID3D12Device13 *)wrap;
        else if(riid == __uuidof(ID3D12Device14))
          *ppDevice = (ID3D12Device14 *)wrap;
      }
    }
    else if(SUCCEEDED(ret))
    {
      RDCLOG("Created wrapped D3D12 device.");
    }
    else
    {
      RDCDEBUG("failed. HRESULT: %s", ToStr(ret).c_str());
    }

    EndRecurse();

    return ret;
  }

  // ksh: shared body. createFunc passed explicitly (d3d12.dll orig or the separate d3d12core.dll
  // orig) to avoid forwarding-shim recursion.
  static HRESULT D3D12CreateDevice_Impl(PFN_D3D12_CREATE_DEVICE createFunc, const char *tag,
                                        const char *fallbackModule, IUnknown *pAdapter,
                                        D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid,
                                        void **ppDevice)
  {
    RDCLOG("[D3D12CreateDevice_hook:%s] CALLED: pAdapter=%p, FeatureLevel=0x%x, riid=%s, "
           "ppDevice=%p, createFunc=%p",
           tag, pAdapter, MinimumFeatureLevel, ToStr(riid).c_str(), ppDevice, createFunc);

    if(!createFunc)
    {
      RDCLOG("[D3D12CreateDevice_hook:%s] no createFunc, resolve via GetProcAddress from %s", tag,
             fallbackModule);
      HMODULE mod = GetModuleHandleA(fallbackModule);

      if(mod)
        createFunc = (PFN_D3D12_CREATE_DEVICE)GetProcAddress(mod, "D3D12CreateDevice");

      if(!createFunc)
      {
        RDCERR("Something went seriously wrong, %s couldn't be loaded!", fallbackModule);
        return E_UNEXPECTED;
      }
    }

    RDCLOG("[D3D12CreateDevice_hook:%s] ====> d3d12hooks.Create_Internal", tag);

    return d3d12hooks.Create_Internal(createFunc, NULL, pAdapter, MinimumFeatureLevel, riid,
                                      ppDevice);
  }

  static HRESULT WINAPI D3D12CreateDevice_hook(IUnknown *pAdapter,
                                               D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid,
                                               void **ppDevice)
  {
    return D3D12CreateDevice_Impl(d3d12hooks.CreateDevice(), "d3d12", "d3d12.dll", pAdapter,
                                  MinimumFeatureLevel, riid, ppDevice);
  }

  static HRESULT WINAPI D3D12CreateDevice_Core_hook(IUnknown *pAdapter,
                                                    D3D_FEATURE_LEVEL MinimumFeatureLevel,
                                                    REFIID riid, void **ppDevice)
  {
    return D3D12CreateDevice_Impl(d3d12hooks.CreateDevice_Core(), "core", "d3d12core.dll", pAdapter,
                                  MinimumFeatureLevel, riid, ppDevice);
  }

  static HRESULT WINAPI D3D12CreateDevice_Interposer_hook(IUnknown *pAdapter,
                                                          D3D_FEATURE_LEVEL MinimumFeatureLevel,
                                                          REFIID riid, void **ppDevice)
  {
    return D3D12CreateDevice_Impl(d3d12hooks.CreateDevice_Interposer(), "sl", "sl.interposer.dll",
                                  pAdapter, MinimumFeatureLevel, riid, ppDevice);
  }

  static HRESULT EnableExperimentalFeatures_Impl(PFN_D3D12_ENABLE_EXPERIMENTAL_FEATURES realFunc,
                                                 const char *tag, UINT NumFeatures, const IID *pIIDs,
                                                 void *pConfigurationStructs,
                                                 UINT *pConfigurationStructSizes)
  {
    RDCLOG("[D3D12EnableExperimentalFeatures_hook:%s] CALLED: NumFeatures=%u, realFunc=%p", tag,
           NumFeatures, realFunc);

    rdcarray<IID> allowedIIDs;

    // allow enabling unsigned DXIL, and GPU upload heaps on most windows versions
    for(UINT i = 0; i < NumFeatures; i++)
    {
      if(pIIDs[i] == D3D12ExperimentalShaderModels)
        allowedIIDs.push_back(D3D12ExperimentalShaderModels);
      else if(pIIDs[i] == D3D12GPUUploadHeapsOnUnsupportedOS)
        allowedIIDs.push_back(D3D12GPUUploadHeapsOnUnsupportedOS);
    }

    // there's no "partially successful" error code, so we just lie to the application and pretend
    // that any filtered IIDs also succeeded
    if(!allowedIIDs.empty() && realFunc)
      return realFunc((UINT)allowedIIDs.size(), allowedIIDs.data(), NULL, NULL);

    // header says "The call returns E_NOINTERFACE if an unrecognized feature is passed in or
    // Windows Developer mode is not on." so this is the most appropriate error for if no IIDs are
    // allowed.
    return E_NOINTERFACE;
  }

  static HRESULT WINAPI D3D12EnableExperimentalFeatures_hook(UINT NumFeatures, const IID *pIIDs,
                                                             void *pConfigurationStructs,
                                                             UINT *pConfigurationStructSizes)
  {
    return EnableExperimentalFeatures_Impl(d3d12hooks.EnableExperimentalFeatures(), "d3d12",
                                           NumFeatures, pIIDs, pConfigurationStructs,
                                           pConfigurationStructSizes);
  }

  static HRESULT WINAPI D3D12EnableExperimentalFeatures_Core_hook(UINT NumFeatures, const IID *pIIDs,
                                                                  void *pConfigurationStructs,
                                                                  UINT *pConfigurationStructSizes)
  {
    return EnableExperimentalFeatures_Impl(d3d12hooks.EnableExperimentalFeatures_Core(), "core",
                                           NumFeatures, pIIDs, pConfigurationStructs,
                                           pConfigurationStructSizes);
  }

  static HRESULT WINAPI D3D12EnableExperimentalFeatures_Interposer_hook(
      UINT NumFeatures, const IID *pIIDs, void *pConfigurationStructs,
      UINT *pConfigurationStructSizes)
  {
    return EnableExperimentalFeatures_Impl(d3d12hooks.EnableExperimentalFeatures_Interposer(), "sl",
                                           NumFeatures, pIIDs, pConfigurationStructs,
                                           pConfigurationStructSizes);
  }

  static HRESULT WINAPI GetD3D11On12On7Interface_hook(ID3D11On12On7 **ppIface)
  {
    RDCLOG("[GetD3D11On12On7Interface_hook] CALLED: ppIface=%p", ppIface);

    ID3D11On12On7 *real = NULL;
    d3d12hooks.GetD3D11On12On7()(&real);
    *ppIface = (ID3D11On12On7 *)(new WrappedD3D11On12On7(real));
    return S_OK;
  }

  static HRESULT D3D12GetDebugInterface_Impl(PFN_D3D12_GET_DEBUG_INTERFACE realFunc, const char *tag,
                                             REFIID riid, void **ppvDebug)
  {
    RDCLOG("[D3D12GetDebugInterface_hook:%s] CALLED: riid=%s, ppvDebug=%p, realFunc=%p", tag,
           ToStr(riid).c_str(), ppvDebug, realFunc);

    if(riid == CLSID_D3D12StateObjectFactory)
    {
      RDCLOG("Deliberately reporting no support for state object factories");
      return E_NOINTERFACE;
    }

    if(!realFunc)
      return E_UNEXPECTED;

    IUnknown *realUnk = NULL;
    HRESULT real = realFunc(riid, (void **)&realUnk);

    if(FAILED(real) || realUnk == NULL)
    {
      if(ppvDebug)
        *ppvDebug = NULL;
      return real;
    }

    HRESULT hr = GetWrappedInterface(realUnk, riid, ppvDebug);

    if(SUCCEEDED(hr))
    {
      realUnk->Release();

      RDCLOG("[D3D12GetDebugInterface_hook:%s] returned wrapped interface, ppvDebug=%p, hr=0x%x",
             tag, ppvDebug ? *ppvDebug : NULL, hr);

      return hr;
    }

    // ksh: pass through unknown-but-valid interfaces rather than dropping them (same rationale as
    // D3D12GetInterface_Impl).
    RDCLOG("[D3D12GetDebugInterface_hook:%s] passthrough (unwrapped) for riid=%s, real=%p", tag,
           ToStr(riid).c_str(), realUnk);
    if(ppvDebug)
      *ppvDebug = realUnk;
    else
      realUnk->Release();
    return real;
  }

  static HRESULT WINAPI D3D12GetDebugInterface_hook(REFIID riid, void **ppvDebug)
  {
    return D3D12GetDebugInterface_Impl(d3d12hooks.GetDebugInterface(), "d3d12", riid, ppvDebug);
  }

  static HRESULT WINAPI D3D12GetDebugInterface_Core_hook(REFIID riid, void **ppvDebug)
  {
    return D3D12GetDebugInterface_Impl(d3d12hooks.GetDebugInterface_Core(), "core", riid, ppvDebug);
  }

  static HRESULT WINAPI D3D12GetDebugInterface_Interposer_hook(REFIID riid, void **ppvDebug)
  {
    return D3D12GetDebugInterface_Impl(d3d12hooks.GetDebugInterface_Interposer(), "sl", riid,
                                       ppvDebug);
  }

  // ksh: shared body for both the d3d12.dll and d3d12core.dll D3D12GetInterface hooks. The correct
  // real function pointer is passed in explicitly (never shared) to avoid the forwarding-shim
  // recursion described on the _Core HookedFunction members.
  static HRESULT D3D12GetInterface_Impl(PFN_D3D12_GET_INTERFACE realFunc, const char *tag,
                                        REFCLSID rclsid, REFIID riid, void **ppvDebug)
  {
    RDCLOG("[D3D12GetInterface_hook:%s] CALLED: rclsid=%s, riid=%s, ppvDebug=%p, realFunc=%p",
           tag, ToStr(rclsid).c_str(), ToStr(riid).c_str(), ppvDebug, realFunc);

    if(riid == CLSID_D3D12StateObjectFactory)
    {
      RDCLOG("Deliberately reporting no support for state object factories");
      return E_NOINTERFACE;
    }

    if(!realFunc)
    {
      RDCERR("[D3D12GetInterface_hook:%s] no real function pointer!", tag);
      return E_UNEXPECTED;
    }

    IUnknown *realUnk = NULL;
    HRESULT real = realFunc(rclsid, riid, (void **)&realUnk);
    RDCLOG("[D3D12GetInterface_hook:%s] real call. realUnk=%p, hr=0x%x", tag, realUnk, real);

    // if the real call failed, just forward the failure (nothing to wrap)
    if(FAILED(real) || realUnk == NULL)
    {
      if(ppvDebug)
        *ppvDebug = NULL;
      return real;
    }

    HRESULT hr = GetWrappedInterface(realUnk, riid, ppvDebug);
	RDCLOG("[D3D12GetInterface_hook:%s] GetWrappedInterface. *ppvDebug=%p, hr=0x%x", tag,
	       ppvDebug ? *ppvDebug : NULL, hr);

    if(SUCCEEDED(hr))
    {
      // GetWrappedInterface took its own ref on realUnk (for the interfaces it wraps), so release
      // our transient reference here.
      realUnk->Release();
      return hr;
    }

    // ksh: GetWrappedInterface doesn't know this riid. CRITICAL: we must NOT drop it - some of
    // these interfaces are load-bearing for the D3D12 runtime, most importantly ID3D12CoreModule
    // which the runtime uses to bootstrap-load the Agility SDK (D3D12Core.dll). Returning
    // E_NOINTERFACE (the old behaviour) with *ppvDebug unset corrupts runtime init and HANGS the
    // game before its window appears. Instead, pass the real interface straight through to the
    // caller unwrapped. We hand our reference to the caller (no extra AddRef/Release needed).
    RDCLOG("[D3D12GetInterface_hook:%s] passthrough (unwrapped) for riid=%s, real=%p", tag,
           ToStr(riid).c_str(), realUnk);

    if(ppvDebug)
      *ppvDebug = realUnk;
    else
      realUnk->Release();

    return real;
  }

  // d3d12.dll variant: uses the d3d12.dll orig pointer
  static HRESULT WINAPI D3D12GetInterface_hook(REFCLSID rclsid, REFIID riid, void **ppvDebug)
  {
    return D3D12GetInterface_Impl(d3d12hooks.GetInterface(), "d3d12", rclsid, riid, ppvDebug);
  }

  // d3d12core.dll variant: uses the SEPARATE d3d12core.dll orig pointer (real non-forwarding
  // export) to avoid shim recursion.
  static HRESULT WINAPI D3D12GetInterface_Core_hook(REFCLSID rclsid, REFIID riid, void **ppvDebug)
  {
    return D3D12GetInterface_Impl(d3d12hooks.GetInterface_Core(), "core", rclsid, riid, ppvDebug);
  }

  // sl.interposer.dll (NVIDIA Streamline) variant.
  static HRESULT WINAPI D3D12GetInterface_Interposer_hook(REFCLSID rclsid, REFIID riid,
                                                          void **ppvDebug)
  {
    return D3D12GetInterface_Impl(d3d12hooks.GetInterface_Interposer(), "sl", rclsid, riid,
                                  ppvDebug);
  }
};

D3D12Hook D3D12Hook::d3d12hooks;

HRESULT CreateD3D12_Internal(RealD3D12CreateFunction real, D3D12DevConfiguration *devConfig,
                             IUnknown *pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid,
                             void **ppDevice)
{
  RDCLOG("[CreateD3D12_Internal] ====> D3D12Hook::d3d12hooks.Create_Internal, adapter=%p, riid=%s",
         pAdapter, ToStr(riid).c_str());

  return D3D12Hook::d3d12hooks.Create_Internal(real, devConfig, pAdapter, MinimumFeatureLevel, riid,
                                               ppDevice);
}

HRESULT STDMETHODCALLTYPE WrappedID3D12DeviceFactory::GetConfigurationInterface(
    REFCLSID clsid, REFIID iid, _COM_Outptr_ void **ppv)
{
  IUnknown *realUnk = NULL;
  HRESULT real = m_pReal->GetConfigurationInterface(clsid, iid, (void **)&realUnk);

  HRESULT hr = D3D12Hook::GetWrappedInterface(realUnk, iid, ppv);

  if(realUnk)
    realUnk->Release();

  if(SUCCEEDED(hr))
    return hr;

  RDCWARN("Unknown UUID passed to D3D12GetDebugInterface: %s. Real call %s succeed (%x).",
          ToStr(iid).c_str(), SUCCEEDED(real) ? "did" : "did not", real);

  return E_NOINTERFACE;
}
