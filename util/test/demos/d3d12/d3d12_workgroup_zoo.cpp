/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2019-2025 Baldur Karlsson
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

#include "3rdparty/fmt/core.h"
#include "d3d12_test.h"

RD_TEST(D3D12_Workgroup_Zoo, D3D12GraphicsTest)
{
  static constexpr const char *Description =
      "Test of behaviour around workgroup operations in shaders.";

  const std::string common = R"EOSHADER(

cbuffer rootconsts : register(b0)
{
  uint root_test;
}

#define IsTest(x) (root_test == x)

)EOSHADER";

  const std::string compCommon = common + R"EOSHADER(

RWStructuredBuffer<float4> outbuf : register(u0);

static uint3 tid;

groupshared uint4 gsmUint4[1024];

void SetOutput(float4 data)
{
  outbuf[root_test * 1024 + tid.y * GROUP_SIZE_X + tid.x] = data;
}

)EOSHADER";

  const std::string comp = compCommon + R"EOSHADER(

float4 funcD(uint id)
{
  return WaveActiveSum(id/2).xxxx;
}

float4 nestedFunc(uint id)
{
  float4 ret = funcD(id/3);
  ret.w = WaveActiveSum(id);
  return ret;
}

float4 funcA(uint id)
{
   return nestedFunc(id*2);
}

float4 funcB(uint id)
{
   return nestedFunc(id*4);
}

float4 funcTest(uint id)
{
  if ((id % 2) == 0)
  {
    return 0.xxxx;
  }
  else
  {
    float value = WaveActiveSum(id);
    if (id < 10)
    {
      return value.xxxx;
    }
    value += WaveActiveSum(id/2);
    return value.xxxx;
  }
}

float4 ComplexPartialReconvergence(uint id)
{
  float4 result = 0.0.xxxx;
  // Loops with different number of iterations per thread
  for (uint i = id; i < 23; i++)
  {
    result.x += WaveActiveSum(id);
  }

  if ((result.x < 5) || (id > 20))
  {
    result.y += WaveActiveSum(id);
    if (id < 25)
      result.z += WaveActiveSum(id);
  }
  else if (result.x < 10)
  {
    result.y += WaveActiveSum(id);

    if (result.x > 5)
      result.z += WaveActiveSum(id);
  }

  result.w += WaveActiveSum(id);

  return result;
}

[numthreads(GROUP_SIZE_X, GROUP_SIZE_Y, 1)]
void main(uint3 inTid : SV_DispatchThreadID)
{
  tid = inTid;
  float4 data = 0.0f.xxxx;
  uint id = WaveGetLaneIndex();
  gsmUint4[id] = id.xxxx;
  SetOutput(data);

  if(IsTest(0))
  {
    data.x = id;
    AllMemoryBarrierWithGroupSync();
  }
  else if(IsTest(1))
  {
    data.x = WaveActiveSum(id);
    AllMemoryBarrierWithGroupSync();
  }
  else if(IsTest(2))
  {
    // Diverged threads which reconverge 
    if (id < 10)
    {
        // active threads 0-9
        data.x = WaveActiveSum(id);

        if ((id % 2) == 0)
          data.y = WaveActiveSum(id);
        else
          data.y = WaveActiveSum(id);

        data.x += WaveActiveSum(id);
    }
    else
    {
        // active threads 10...
        data.x = WaveActiveSum(id);
    }
    data.y = WaveActiveSum(id);
    AllMemoryBarrierWithGroupSync();
  }
  else if(IsTest(3))
  {
    // Converged threads calling a function 
    data = funcTest(id);
    data.y = WaveActiveSum(id);
    AllMemoryBarrierWithGroupSync();
  }
  else if(IsTest(4))
  {
    // Converged threads calling a function which has a nested function call in it
    data = nestedFunc(id);
    data.y = WaveActiveSum(id);
    AllMemoryBarrierWithGroupSync();
  }
  else if(IsTest(5))
  {
    // Diverged threads calling the same function
    if (id < 10)
    {
      data = funcD(id);
    }
    else
    {
      data = funcD(id);
    }
    data.y = WaveActiveSum(id);
    AllMemoryBarrierWithGroupSync();
  }
  else if(IsTest(6))
  {
    // Diverged threads calling the same function which has a nested function call in it
    if (id < 10)
    {
      data = funcA(id);
    }
    else
    {
      data = funcB(id);
    }
    data.y = WaveActiveSum(id);
    AllMemoryBarrierWithGroupSync();
  }
  else if(IsTest(7))
  {
    // Diverged threads which early exit
    if (id < 10)
    {
      data.x = WaveActiveSum(id+10);
      SetOutput(data);
      return;
    }
    data.x = WaveActiveSum(id);
  }
  else if(IsTest(8))
  {
     // Loops with different number of iterations per thread
    for (uint i = 0; i < id; i++)
    {
      data.x += WaveActiveSum(id);
    }
    AllMemoryBarrierWithGroupSync();
  }
  else if(IsTest(9))
  {
    // Query functions : unit tests
    data.x = float(WaveGetLaneCount());
    data.y = float(WaveGetLaneIndex());
    data.z = float(WaveIsFirstLane());
    AllMemoryBarrierWithGroupSync();
  }
  else if(IsTest(10))
  {
    data = ComplexPartialReconvergence(id);

    AllMemoryBarrierWithGroupSync();
  }

  SetOutput(data);
}

)EOSHADER";

  void Prepare(int argc, char **argv)
  {
    D3D12GraphicsTest::Prepare(argc, argv);

    if(opts1.WaveLaneCountMax < 16)
      Avail = "Subgroup size is less than 16";

    bool supportSM60 = (m_HighestShaderModel >= D3D_SHADER_MODEL_6_0) && m_DXILSupport;
    if(!supportSM60)
      Avail = "SM 6.0 not supported";
  }

  int main()
  {
    // initialise, create window, create device, etc
    if(!Init())
      return 3;

    ID3D12RootSignaturePtr sig = MakeSig({constParam(D3D12_SHADER_VISIBILITY_ALL, 0, 0, 1),
                                          uavParam(D3D12_SHADER_VISIBILITY_ALL, 0, 0)});

    const uint32_t imgDim = 128;

    ID3D12ResourcePtr fltTex = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, imgDim, imgDim)
                                   .RTV()
                                   .InitialState(D3D12_RESOURCE_STATE_RENDER_TARGET);
    fltTex->SetName(L"fltTex");
    D3D12_CPU_DESCRIPTOR_HANDLE fltRTV = MakeRTV(fltTex).CreateCPU(0);
    D3D12_GPU_DESCRIPTOR_HANDLE fltSRV = MakeSRV(fltTex).CreateGPU(8);

    int32_t numCompTests = 0;

    size_t pos = 0;
    while(pos != std::string::npos)
    {
      pos = comp.find("IsTest(", pos);
      if(pos == std::string::npos)
        break;
      pos += sizeof("IsTest(") - 1;
      numCompTests = std::max(numCompTests, atoi(comp.c_str() + pos) + 1);
    }

    struct
    {
      int x, y;
    } compsize[] = {
        {70, 1},
    };
    std::string comppipe_name[ARRAY_COUNT(compsize)];
    ID3D12PipelineStatePtr comppipe[ARRAY_COUNT(compsize)];

    std::string defines;

    for(int i = 0; i < ARRAY_COUNT(comppipe); i++)
    {
      std::string sizedefine;
      sizedefine = fmt::format("#define GROUP_SIZE_X {}\n#define GROUP_SIZE_Y {}\n", compsize[i].x,
                               compsize[i].y);
      comppipe_name[i] = fmt::format("{}x{}", compsize[i].x, compsize[i].y);

      comppipe[i] =
          MakePSO().RootSig(sig).CS(Compile(defines + sizedefine + comp, "main", "cs_6_0"));
      comppipe[i]->SetName(UTF82Wide(comppipe_name[i]).c_str());
    }

    ID3D12ResourcePtr bufOut = MakeBuffer().Size(sizeof(Vec4f) * 1024 * numCompTests).UAV();
    D3D12ViewCreator uavView =
        MakeUAV(bufOut).Format(DXGI_FORMAT_R32_UINT).NumElements(4 * 1024 * numCompTests);
    D3D12_CPU_DESCRIPTOR_HANDLE uavcpu = uavView.CreateClearCPU(10);
    D3D12_GPU_DESCRIPTOR_HANDLE uavgpu = uavView.CreateGPU(10);

    bufOut->SetName(L"bufOut");

    while(Running())
    {
      ID3D12GraphicsCommandListPtr cmd = GetCommandBuffer();

      Reset(cmd);

      cmd->SetDescriptorHeaps(1, &m_CBVUAVSRV.GetInterfacePtr());

      ID3D12ResourcePtr bb = StartUsingBackbuffer(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);

      ClearRenderTargetView(cmd, BBRTV, {0.2f, 0.2f, 0.2f, 1.0f});

      pushMarker(cmd, "Compute Tests");

      for(size_t p = 0; p < ARRAY_COUNT(comppipe); p++)
      {
        ResourceBarrier(cmd);

        UINT zero[4] = {};
        cmd->ClearUnorderedAccessViewUint(uavgpu, uavcpu, bufOut, zero, 0, NULL);

        ResourceBarrier(cmd);
        pushMarker(cmd, comppipe_name[p]);

        cmd->SetPipelineState(comppipe[p]);
        cmd->SetComputeRootSignature(sig);
        cmd->SetComputeRootUnorderedAccessView(1, bufOut->GetGPUVirtualAddress());

        for(int i = 0; i < numCompTests; i++)
        {
          cmd->SetComputeRoot32BitConstant(0, i, 0);
          cmd->Dispatch(2, 1, 1);
        }

        popMarker(cmd);
      }

      popMarker(cmd);

      FinishUsingBackbuffer(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);

      cmd->Close();

      SubmitAndPresent({cmd});
    }

    return 0;
  }
};

REGISTER_TEST();
