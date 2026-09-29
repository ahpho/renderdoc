# 《未眠夜》EAT / Forwarder PE 镜像一致性防护

> 一句话总结：从可信磁盘 PE 中取得关键导出的原始 EAT 值，与进程内已加载映像逐项比较；如果导出被改写并指向模块映像之外的 `MEM_PRIVATE + executable` 页面，就可以高置信度识别本次使用的 EAT relay 截帧方式。

## 1. 本文只解决什么问题

本文只聚焦以下已知链路：

```text
提前加载 DXGI / D3D12 / Streamline
  -> 修改已加载模块的 Export Address Table（EAT）
  -> 让关键导出指向模块外的 relay
  -> relay 再跳转到捕获模块中的 hook
```

这里不展开通用反注入、驱动层反作弊或服务端风控。目标是回答三个问题：

1. 如何准确发现 EAT 或 forwarder 被修改；
2. 为什么这个检测可以识别本次方法；
3. 如何避免把合法 forwarder、ASLR 或系统版本差异误判为攻击。

## 2. 为什么 EAT 是这次攻击必须留下的痕迹

PE 导出表中的 `AddressOfFunctions` 是一个 `DWORD` 数组，每个元素保存相对于模块基址的 RVA。调用方按名称或 ordinal 找到对应元素后，通常按以下方式计算目标：

```text
target = moduleBase + exportRva
```

本次链路需要让游戏或 Streamline 取得捕获模块的 hook，而不是真实 DXGI/D3D12 入口。既然调用方会读取 EAT，攻击实现至少要满足以下条件之一：

- 修改 EAT RVA，使其指向一个 relay；
- 把原本的代码导出改造成 forwarder；
- 修改原有 forwarder 字符串，使其转发到别的模块或导出。

这些修改都发生在可观察的 PE 导出语义上。工具名称、DLL 文件名、窗口标题即使全部改变，内存中的导出结果仍必须与可信原始值不同，才能把控制流导向 hook。

因此，防守方不需要猜测“是不是 RenderDoc”，只需要验证：

```text
可信文件中的导出语义 == 当前内存中的导出语义
```

如果不相等，再检查新目标属于哪一种内存，就能进一步区分正常差异与 relay。

## 3. 不要比较整个磁盘文件与内存映像

磁盘上的 PE 是 raw-file layout，加载后的模块是 image layout，两者不能直接逐字节比较：

- 节在磁盘中按 `PointerToRawData` 布局，在内存中按 `VirtualAddress` 布局；
- ASLR 会改变模块基址；
- 重定位、IAT 绑定、加载器写入和页面保护可能造成合法差异；
- 部分节在磁盘和内存中的尺寸不同。

正确做法是分别解析两个视图，然后比较“导出语义”：

| 内容 | 可信磁盘 PE | 已加载内存映像 |
| --- | --- | --- |
| Export Directory | RVA 转换为文件偏移后读取 | `moduleBase + RVA` 读取 |
| 导出函数表 | 读取原始 `DWORD RVA` | 读取当前 `DWORD RVA` |
| 普通导出 | 比较 RVA | 比较 RVA |
| Forwarder | 比较字符串并递归验证 | 比较字符串并递归验证 |
| 最终目标 | 不执行文件内容 | 用 `VirtualQuery` 验证内存归属 |

EAT 保存的是 RVA，所以 ASLR 只改变 `moduleBase`，不会改变可信 EAT RVA。系统升级可能改变 RVA，因此不要把某个 Windows 版本的 RVA 永久硬编码；应从当前机器上经过身份验证的系统文件建立基线。

## 4. 可信基线必须先可信

如果直接相信“当前已加载模块报告的任意同名文件”，攻击者可以提供一份同样被修改的磁盘文件，使比较失去意义。

建立基线时至少验证：

### 4.1 Windows 系统模块

- 规范化路径属于真实的 System32；
- 文件具有有效的 Microsoft 签名；
- 打开的文件 ID、卷 ID 与最终解析对象一致；
- 验证期间保持文件句柄，避免路径被替换；
- 不通过 `LoadLibrary` 再执行一份所谓“干净 DLL”。

### 4.2 游戏或 Streamline 模块

- 规范化路径位于允许的安装目录；
- SHA-256 与签名发布清单一致；
- Authenticode 发布者属于允许集合；
- 文件 ID 与已加载模块的映射来源一致。

文件签名只说明签名链和内容完整性，不能替代发布者白名单。拥有其他合法代码签名证书的文件不应自动成为可信基线。

## 5. EAT 与 forwarder 的判定规则

### 5.1 普通代码导出

如果 EAT 中的函数 RVA 不位于 Export Directory 自身的 RVA 范围内，它通常是代码或数据导出。对于已知图形 API 函数，应进一步要求：

- 内存 RVA 等于可信磁盘 RVA；
- RVA 小于模块 `SizeOfImage`；
- `moduleBase + RVA` 不发生整数溢出；
- `VirtualQuery` 返回 `MEM_COMMIT`；
- 页面类型为 `MEM_IMAGE`；
- `AllocationBase` 等于该模块基址；
- RVA 位于该模块的可执行节中；
- 页面保护允许执行，且不应为可写可执行页面。

### 5.2 Forwarder

如果函数 RVA 落在 Export Directory 的 RVA 范围内，该 EAT entry 表示 forwarder string，而不是代码地址，例如：

```text
D3D12Core.D3D12CreateDevice
KERNELBASE.SomeFunction
MODULE.#123
```

此时必须：

1. 比较可信文件与内存中的 forwarder 字符串；
2. 按 Windows/API-set 规则解析目标模块；
3. 验证目标模块的路径、签名或 manifest；
4. 递归验证目标模块中的名称或 ordinal；
5. 设置最大深度并记录 visited 集合，防止循环；
6. 不因扫描而自动加载一个尚未加载的不可信模块。

不能把合法 forwarder 当成“导出地址越界”，也不能只比较第一层字符串后就停止，因为被转发模块的 EAT 仍可能被修改。

## 6. 针对本次 relay 的高置信度判据

本次 relay 必须被编码为一个 32 位 RVA。其目标通常位于模块基址之后 4 GiB 的可表示范围内，但在模块自身 `SizeOfImage` 之外，并由攻击方分配为私有可执行内存。

最关键的组合判据是：

```text
memoryEAT != trustedFileEAT
AND target 不在所属模块 SizeOfImage 内
AND VirtualQuery(target).Type == MEM_PRIVATE
AND target 页面允许执行
```

如果还能确认该页面是一段短 relay，并最终跳转到未批准模块，则置信度更高。但不应依赖固定机器码，因为 relay 可以使用多种等价指令实现。

仅发现 `MEM_PRIVATE + executable` 不足以处罚：JIT、保护壳或某些中间件也会生成动态代码。关键在于它同时成为了受保护系统导出的新 EAT 目标。

## 7. 接近可编译的 C++ 参考实现

下面是一组连贯的 Windows x64 / C++17 函数。它不是逐段伪代码：复制到一个 `.cpp` 后，除产品信任策略外，PE 解析、EAT 对比、forwarder 递归和内存分类都已经连通。

为了让代码保持可审计，示例有意限定为当前游戏实际使用的 PE32+。如果还需要支持 32 位进程，应另外实现 `IMAGE_OPTIONAL_HEADER32` 分支，不要通过强制类型转换混用两种格式。

```cpp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#include <Softpub.h>
#include <WinTrust.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "Bcrypt.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "Wintrust.lib")

namespace eat_guard
{
enum class ExportKind
{
  Missing,
  CodeRva,
  Forwarder,
  Malformed,
};

enum class Finding
{
  Clean,
  ModuleUntrusted,
  ExportMissing,
  ExportMalformed,
  ExportKindChanged,
  ForwarderChanged,
  EatChangedInsideImage,
  EatPointsToPrivateExecutable,
  EatPointsToOtherImage,
  TargetNotExecutable,
  ForwarderCycle,
  Indeterminate,
};

struct Symbol
{
  std::string name;
  std::uint16_t ordinal = 0;
  bool byOrdinal = false;

  static Symbol Name(const char *value) { return {value, 0, false}; }
  static Symbol Ordinal(std::uint16_t value) { return {{}, value, true}; }

  std::string Key() const
  {
    return byOrdinal ? "#" + std::to_string(ordinal) : name;
  }
};

struct ExportRecord
{
  ExportKind kind = ExportKind::Malformed;
  std::uint32_t rva = 0;
  std::string forwarder;
};

struct Result
{
  Finding finding = Finding::Indeterminate;
  HMODULE module = nullptr;
  Symbol symbol;
  std::uint32_t expectedRva = 0;
  std::uint32_t observedRva = 0;
  const void *target = nullptr;
};

// 必须由产品接入：验证规范化路径、文件 ID、签名发布者以及版本化 hash manifest。
// fileHandle 在回调期间保持打开，避免只按一个可被替换的路径进行验证。
using VerifyModuleFile = bool (*)(HMODULE loadedModule, const wchar_t *path, HANDLE fileHandle);

// 只能返回已经加载的模块，不能为了完成扫描而 LoadLibrary 一个新模块。
using ResolveLoadedModule = HMODULE (*)(const wchar_t *moduleName);

struct Policy
{
  VerifyModuleFile verifyModuleFile = nullptr;
  ResolveLoadedModule resolveLoadedModule = nullptr;
  unsigned maxForwardDepth = 8;
};

using Sha256 = std::array<std::uint8_t, 32>;

struct TrustedModuleRule
{
  std::wstring fileName;

  // 必须是通过目录句柄和 GetFinalPathNameByHandleW 得到的规范化路径。
  // Windows 系统 DLL 应指向真实 System32；产品 DLL 应指向受控安装目录。
  std::wstring allowedRoot;
  bool allowSubdirectories = false;

  // 0 表示不按大小约束。系统 DLL 随 Windows 更新变化时通常设为 0。
  std::uint64_t expectedSize = 0;

  // 游戏自有 DLL 和固定版本中间件建议强制 hash；系统 DLL 可依赖路径和签名者规则。
  bool requireFileSha256 = false;
  Sha256 expectedFileSha256 = {};

  // Authenticode 最终签名者证书的 SHA-256 指纹允许列表。证书轮换时在已签名
  // manifest 中并列保留新旧指纹，完成灰度后再移除旧指纹。
  std::vector<Sha256> allowedSignerCertSha256;
};

struct TrustedManifest
{
  std::uint64_t manifestVersion = 0;
  std::vector<TrustedModuleRule> modules;
};

class ScopedHandle
{
public:
  ScopedHandle() = default;
  explicit ScopedHandle(HANDLE value) : m_Value(value) {}
  ~ScopedHandle()
  {
    if(m_Value != nullptr && m_Value != INVALID_HANDLE_VALUE)
      CloseHandle(m_Value);
  }

  ScopedHandle(const ScopedHandle &) = delete;
  ScopedHandle &operator=(const ScopedHandle &) = delete;

  ScopedHandle(ScopedHandle &&other) noexcept
      : m_Value(std::exchange(other.m_Value, INVALID_HANDLE_VALUE))
  {
  }

  ScopedHandle &operator=(ScopedHandle &&other) noexcept
  {
    if(this != &other)
    {
      if(m_Value != nullptr && m_Value != INVALID_HANDLE_VALUE)
        CloseHandle(m_Value);
      m_Value = std::exchange(other.m_Value, INVALID_HANDLE_VALUE);
    }
    return *this;
  }

  HANDLE Get() const { return m_Value; }
  bool Valid() const { return m_Value != nullptr && m_Value != INVALID_HANDLE_VALUE; }

private:
  HANDLE m_Value = INVALID_HANDLE_VALUE;
};

class MappedFile
{
public:
  ~MappedFile()
  {
    if(m_Data)
      UnmapViewOfFile(m_Data);
  }

  MappedFile(const MappedFile &) = delete;
  MappedFile &operator=(const MappedFile &) = delete;
  MappedFile() = default;

  bool OpenForModule(HMODULE module, const Policy &policy)
  {
    // 32768 足以容纳扩展长度路径；生产代码也可以改成动态扩容循环。
    std::vector<wchar_t> path(32768, L'\0');
    DWORD pathLength = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if(pathLength == 0 || pathLength >= path.size())
      return false;

    m_File = ScopedHandle(CreateFileW(path.data(), GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if(!m_File.Valid())
      return false;

    // 默认拒绝，而不是在忘记接入签名/manifest 策略时静默信任。
    if(!policy.verifyModuleFile ||
       !policy.verifyModuleFile(module, path.data(), m_File.Get()))
      return false;

    LARGE_INTEGER fileSize = {};
    if(!GetFileSizeEx(m_File.Get(), &fileSize) || fileSize.QuadPart <= 0 ||
       static_cast<unsigned long long>(fileSize.QuadPart) >
           static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max()))
      return false;

    m_Size = static_cast<std::size_t>(fileSize.QuadPart);
    m_Mapping = ScopedHandle(
        CreateFileMappingW(m_File.Get(), nullptr, PAGE_READONLY, 0, 0, nullptr));
    if(!m_Mapping.Valid())
      return false;

    m_Data = static_cast<const std::uint8_t *>(
        MapViewOfFile(m_Mapping.Get(), FILE_MAP_READ, 0, 0, 0));
    return m_Data != nullptr;
  }

  const std::uint8_t *Data() const { return m_Data; }
  std::size_t Size() const { return m_Size; }

private:
  ScopedHandle m_File;
  ScopedHandle m_Mapping;
  const std::uint8_t *m_Data = nullptr;
  std::size_t m_Size = 0;
};

// manifest 必须在安装后保持只读，并存活到所有校验线程结束。这个 setter 应只在启动
// 初始化阶段调用一次；若项目需要运行时热更新，请改为带锁的 shared_ptr 快照。
static const TrustedManifest *g_VerifiedManifest = nullptr;

void SetVerifiedTrustManifest(const TrustedManifest *manifest)
{
  g_VerifiedManifest = manifest;
}

static bool EqualPathNoCase(const std::wstring &left, const std::wstring &right)
{
  if(left.size() != right.size())
    return false;
  return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()), right.c_str(),
                              static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

static bool GetFinalPath(HANDLE file, DWORD volumeNameMode, std::wstring &path)
{
  std::vector<wchar_t> buffer(32768, L'\0');
  const DWORD length = GetFinalPathNameByHandleW(
      file, buffer.data(), static_cast<DWORD>(buffer.size()),
      FILE_NAME_NORMALIZED | volumeNameMode);
  if(length == 0 || length >= buffer.size())
    return false;
  path.assign(buffer.data(), length);
  return true;
}

static bool GetMappedImageNtPath(HMODULE module, std::wstring &path)
{
  std::vector<wchar_t> buffer(32768, L'\0');
  const DWORD length = GetMappedFileNameW(GetCurrentProcess(), module, buffer.data(),
                                          static_cast<DWORD>(buffer.size()));
  if(length == 0 || length >= buffer.size())
    return false;
  path.assign(buffer.data(), length);
  return true;
}

static std::wstring BaseName(const std::wstring &path)
{
  const std::size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

static bool PathMatchesRoot(const std::wstring &path, const TrustedModuleRule &rule)
{
  std::wstring root = rule.allowedRoot;
  if(root.empty())
    return false;
  while(root.size() > 1 && (root.back() == L'\\' || root.back() == L'/'))
    root.pop_back();

  if(path.size() <= root.size() ||
     CompareStringOrdinal(path.c_str(), static_cast<int>(root.size()), root.c_str(),
                          static_cast<int>(root.size()), TRUE) != CSTR_EQUAL)
    return false;

  if(path[root.size()] != L'\\' && path[root.size()] != L'/')
    return false;

  if(rule.allowSubdirectories)
    return true;

  const std::wstring relative = path.substr(root.size() + 1);
  return relative.find_first_of(L"\\/") == std::wstring::npos;
}

static bool HashFileSha256(HANDLE file, Sha256 &digest)
{
  LARGE_INTEGER originalPosition = {};
  LARGE_INTEGER zero = {};
  if(!SetFilePointerEx(file, zero, &originalPosition, FILE_CURRENT) ||
     !SetFilePointerEx(file, zero, nullptr, FILE_BEGIN))
    return false;

  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  bool success = false;

  do
  {
    if(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
      break;

    DWORD objectSize = 0;
    DWORD hashSize = 0;
    DWORD returned = 0;
    if(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                         reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &returned,
                         0) != 0 ||
       BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                         reinterpret_cast<PUCHAR>(&hashSize), sizeof(hashSize), &returned, 0) != 0 ||
       hashSize != digest.size())
      break;

    std::vector<UCHAR> hashObject(objectSize);
    if(BCryptCreateHash(algorithm, &hash, hashObject.data(), objectSize, nullptr, 0, 0) != 0)
      break;

    std::vector<UCHAR> buffer(64 * 1024);
    for(;;)
    {
      DWORD bytesRead = 0;
      if(!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr))
        break;
      if(bytesRead == 0)
      {
        success = BCryptFinishHash(hash, digest.data(),
                                   static_cast<ULONG>(digest.size()), 0) == 0;
        break;
      }
      if(BCryptHashData(hash, buffer.data(), bytesRead, 0) != 0)
        break;
    }
  } while(false);

  if(hash)
    BCryptDestroyHash(hash);
  if(algorithm)
    BCryptCloseAlgorithmProvider(algorithm, 0);

  // 无论成功失败都恢复文件位置，避免干扰调用者。
  SetFilePointerEx(file, originalPosition, nullptr, FILE_BEGIN);
  return success;
}

static bool VerifyAuthenticodeAndGetSigner(HANDLE file, const std::wstring &path,
                                           Sha256 &signerCertificateSha256)
{
  WINTRUST_FILE_INFO fileInfo = {};
  fileInfo.cbStruct = sizeof(fileInfo);
  fileInfo.pcwszFilePath = path.c_str();
  fileInfo.hFile = file;

  WINTRUST_DATA trustData = {};
  trustData.cbStruct = sizeof(trustData);
  trustData.dwUIChoice = WTD_UI_NONE;
  trustData.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
  trustData.dwUnionChoice = WTD_CHOICE_FILE;
  trustData.pFile = &fileInfo;
  trustData.dwStateAction = WTD_STATEACTION_VERIFY;
  trustData.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;

  GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
  const LONG trustStatus = WinVerifyTrust(nullptr, &action, &trustData);
  bool success = false;

  if(trustStatus == ERROR_SUCCESS && trustData.hWVTStateData)
  {
    CRYPT_PROVIDER_DATA *provider =
        WTHelperProvDataFromStateData(trustData.hWVTStateData);
    CRYPT_PROVIDER_SGNR *signer =
        provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;

    if(signer && signer->csCertChain > 0 && signer->pasCertChain &&
       signer->pasCertChain[0].pCert)
    {
      DWORD digestSize = static_cast<DWORD>(signerCertificateSha256.size());
      success = CertGetCertificateContextProperty(
                    signer->pasCertChain[0].pCert, CERT_SHA256_HASH_PROP_ID,
                    signerCertificateSha256.data(), &digestSize) != FALSE &&
                digestSize == signerCertificateSha256.size();
    }
  }

  if(trustData.hWVTStateData)
  {
    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &trustData);
  }
  return success;
}

bool VerifyAgainstProductManifest(HMODULE loadedModule, const wchar_t *reportedPath,
                                  HANDLE pinnedFile)
{
  (void)reportedPath; // 只把它用于诊断；安全判断以文件句柄解析出的最终路径为准。

  const TrustedManifest *manifest = g_VerifiedManifest;
  if(!manifest || manifest->manifestVersion == 0 || !loadedModule || !pinnedFile ||
     pinnedFile == INVALID_HANDLE_VALUE)
    return false;

  // DOS 最终路径用于目录规则；NT 最终路径用于绑定当前已加载的 image mapping。
  std::wstring finalDosPath;
  std::wstring finalNtPath;
  std::wstring mappedNtPath;
  if(!GetFinalPath(pinnedFile, VOLUME_NAME_DOS, finalDosPath) ||
     !GetFinalPath(pinnedFile, VOLUME_NAME_NT, finalNtPath) ||
     !GetMappedImageNtPath(loadedModule, mappedNtPath) ||
     !EqualPathNoCase(finalNtPath, mappedNtPath))
    return false;

  LARGE_INTEGER fileSize = {};
  if(!GetFileSizeEx(pinnedFile, &fileSize) || fileSize.QuadPart < 0)
    return false;

  Sha256 signerCertificateSha256 = {};
  if(!VerifyAuthenticodeAndGetSigner(pinnedFile, finalDosPath,
                                     signerCertificateSha256))
    return false;

  const std::wstring fileName = BaseName(finalDosPath);
  Sha256 fileSha256 = {};
  bool fileHashCalculated = false;
  bool fileHashValid = false;

  for(const TrustedModuleRule &rule : manifest->modules)
  {
    if(!EqualPathNoCase(fileName, rule.fileName) || !PathMatchesRoot(finalDosPath, rule))
      continue;

    if(rule.expectedSize != 0 &&
       static_cast<std::uint64_t>(fileSize.QuadPart) != rule.expectedSize)
      continue;

    if(rule.allowedSignerCertSha256.empty() ||
       std::find(rule.allowedSignerCertSha256.begin(), rule.allowedSignerCertSha256.end(),
                 signerCertificateSha256) == rule.allowedSignerCertSha256.end())
      continue;

    if(rule.requireFileSha256)
    {
      if(!fileHashCalculated)
      {
        fileHashValid = HashFileSha256(pinnedFile, fileSha256);
        fileHashCalculated = true;
      }
      if(!fileHashValid || fileSha256 != rule.expectedFileSha256)
        continue;
    }

    return true;
  }

  return false;
}

static bool IsReadableMemoryRange(const void *address, std::size_t length)
{
  if(length == 0)
    return true;

  const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(address);
  if(length > std::numeric_limits<std::uintptr_t>::max() - start)
    return false;
  const std::uintptr_t end = start + length;

  std::uintptr_t current = start;
  while(current < end)
  {
    MEMORY_BASIC_INFORMATION memory = {};
    if(VirtualQuery(reinterpret_cast<const void *>(current), &memory, sizeof(memory)) !=
           sizeof(memory) ||
       memory.State != MEM_COMMIT ||
       (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
      return false;

    switch(memory.Protect & 0xff)
    {
      case PAGE_READONLY:
      case PAGE_READWRITE:
      case PAGE_WRITECOPY:
      case PAGE_EXECUTE_READ:
      case PAGE_EXECUTE_READWRITE:
      case PAGE_EXECUTE_WRITECOPY: break;
      default: return false;
    }

    const std::uintptr_t regionBase =
        reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    if(memory.RegionSize > std::numeric_limits<std::uintptr_t>::max() - regionBase)
      return false;
    const std::uintptr_t regionEnd = regionBase + memory.RegionSize;
    if(regionEnd <= current)
      return false;
    current = std::min(end, regionEnd);
  }

  return true;
}

class PeView
{
public:
  enum class Layout
  {
    RawFile,
    MappedImage,
  };

  PeView(const void *base, std::size_t size, Layout layout)
      : m_Base(static_cast<const std::uint8_t *>(base)), m_Size(size), m_Layout(layout)
  {
  }

  bool Init()
  {
    const auto *dos = AtOffset<IMAGE_DOS_HEADER>(0);
    if(!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
      return false;

    const std::size_t ntOffset = static_cast<std::size_t>(dos->e_lfanew);
    const auto *signature = AtOffset<DWORD>(ntOffset);
    const auto *fileHeader = AtOffset<IMAGE_FILE_HEADER>(ntOffset + sizeof(DWORD));
    if(!signature || *signature != IMAGE_NT_SIGNATURE || !fileHeader ||
       fileHeader->NumberOfSections == 0 || fileHeader->NumberOfSections > 96 ||
       fileHeader->SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64))
      return false;

    const std::size_t optionalOffset =
        ntOffset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
    m_Optional = AtOffset<IMAGE_OPTIONAL_HEADER64>(optionalOffset);
    if(!m_Optional || m_Optional->Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
       m_Optional->SizeOfImage == 0)
      return false;

    const std::size_t sectionOffset = optionalOffset + fileHeader->SizeOfOptionalHeader;
    const std::size_t sectionBytes =
        static_cast<std::size_t>(fileHeader->NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
    if(!RangeInBuffer(sectionOffset, sectionBytes))
      return false;

    m_Sections = reinterpret_cast<const IMAGE_SECTION_HEADER *>(m_Base + sectionOffset);
    m_SectionCount = fileHeader->NumberOfSections;
    return true;
  }

  std::uint32_t SizeOfImage() const { return m_Optional->SizeOfImage; }

  template <typename T>
  const T *AtRva(std::uint32_t rva, std::size_t count = 1) const
  {
    if(count > std::numeric_limits<std::size_t>::max() / sizeof(T))
      return nullptr;
    return static_cast<const T *>(RvaBytes(rva, count * sizeof(T)));
  }

  bool ReadCString(std::uint32_t rva, std::string &result, std::size_t limit = 4096) const
  {
    result.clear();
    for(std::size_t i = 0; i < limit; ++i)
    {
      const std::uint64_t current = static_cast<std::uint64_t>(rva) + i;
      if(current > std::numeric_limits<std::uint32_t>::max())
        return false;

      const char *value = AtRva<char>(static_cast<std::uint32_t>(current));
      if(!value)
        return false;
      if(*value == '\0')
        return true;
      result.push_back(*value);
    }
    return false;
  }

  bool IsExecutableRva(std::uint32_t rva) const
  {
    for(std::size_t i = 0; i < m_SectionCount; ++i)
    {
      const IMAGE_SECTION_HEADER &section = m_Sections[i];
      const std::uint64_t start = section.VirtualAddress;
      const std::uint64_t length =
          std::max(section.Misc.VirtualSize, section.SizeOfRawData);
      const std::uint64_t end = start + length;
      if(rva >= start && rva < end)
        return (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
    }
    return false;
  }

  ExportRecord FindExport(const Symbol &symbol) const
  {
    if(!m_Optional || m_Optional->NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT)
      return {ExportKind::Malformed, 0, {}};

    const IMAGE_DATA_DIRECTORY &data =
        m_Optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if(data.VirtualAddress == 0 || data.Size < sizeof(IMAGE_EXPORT_DIRECTORY))
      return {ExportKind::Missing, 0, {}};

    const auto *directory = AtRva<IMAGE_EXPORT_DIRECTORY>(data.VirtualAddress);
    if(!directory || directory->NumberOfFunctions == 0 ||
       directory->NumberOfFunctions > 1000000 || directory->NumberOfNames > 1000000)
      return {ExportKind::Malformed, 0, {}};

    const DWORD *functions =
        AtRva<DWORD>(directory->AddressOfFunctions, directory->NumberOfFunctions);
    if(!functions)
      return {ExportKind::Malformed, 0, {}};

    DWORD functionIndex = std::numeric_limits<DWORD>::max();
    if(symbol.byOrdinal)
    {
      if(symbol.ordinal < directory->Base)
        return {ExportKind::Missing, 0, {}};
      functionIndex = static_cast<DWORD>(symbol.ordinal - directory->Base);
    }
    else
    {
      const DWORD *names = AtRva<DWORD>(directory->AddressOfNames, directory->NumberOfNames);
      const WORD *ordinals =
          AtRva<WORD>(directory->AddressOfNameOrdinals, directory->NumberOfNames);
      if(!names || !ordinals)
        return {ExportKind::Malformed, 0, {}};

      for(DWORD i = 0; i < directory->NumberOfNames; ++i)
      {
        std::string currentName;
        if(!ReadCString(names[i], currentName))
          return {ExportKind::Malformed, 0, {}};
        if(currentName == symbol.name)
        {
          functionIndex = ordinals[i];
          break;
        }
      }
    }

    if(functionIndex == std::numeric_limits<DWORD>::max() ||
       functionIndex >= directory->NumberOfFunctions || functions[functionIndex] == 0)
      return {ExportKind::Missing, 0, {}};

    const DWORD functionRva = functions[functionIndex];
    const std::uint64_t exportStart = data.VirtualAddress;
    const std::uint64_t exportEnd = exportStart + data.Size;
    if(functionRva >= exportStart && functionRva < exportEnd)
    {
      std::string forwarder;
      if(!ReadCString(functionRva, forwarder) || forwarder.empty())
        return {ExportKind::Malformed, 0, {}};
      return {ExportKind::Forwarder, functionRva, std::move(forwarder)};
    }

    // 不在这里拒绝越过 SizeOfImage 的 live RVA：这正是要上报的 relay 特征。
    return {ExportKind::CodeRva, functionRva, {}};
  }

private:
  template <typename T>
  const T *AtOffset(std::size_t offset) const
  {
    if(!RangeInBuffer(offset, sizeof(T)))
      return nullptr;
    return reinterpret_cast<const T *>(m_Base + offset);
  }

  bool RangeInBuffer(std::size_t offset, std::size_t length) const
  {
    return offset <= m_Size && length <= m_Size - offset;
  }

  const void *RvaBytes(std::uint32_t rva, std::size_t length) const
  {
    if(m_Layout == Layout::MappedImage)
    {
      const std::size_t imageSize = std::min<std::size_t>(m_Size, m_Optional->SizeOfImage);
      if(rva > imageSize || length > imageSize - rva)
        return nullptr;
      if(!IsReadableMemoryRange(m_Base + rva, length))
        return nullptr;
      return m_Base + rva;
    }

    // raw-file layout：头部 RVA 可以直接作为文件偏移。
    if(rva < m_Optional->SizeOfHeaders)
    {
      if(!RangeInBuffer(rva, length) || length > m_Optional->SizeOfHeaders - rva)
        return nullptr;
      return m_Base + rva;
    }

    for(std::size_t i = 0; i < m_SectionCount; ++i)
    {
      const IMAGE_SECTION_HEADER &section = m_Sections[i];
      if(rva < section.VirtualAddress)
        continue;

      const std::uint64_t delta =
          static_cast<std::uint64_t>(rva) - section.VirtualAddress;
      if(delta > section.SizeOfRawData || length > section.SizeOfRawData - delta)
        continue;

      const std::uint64_t offset =
          static_cast<std::uint64_t>(section.PointerToRawData) + delta;
      if(offset > std::numeric_limits<std::size_t>::max() ||
         !RangeInBuffer(static_cast<std::size_t>(offset), length))
        return nullptr;
      return m_Base + static_cast<std::size_t>(offset);
    }

    return nullptr;
  }

  const std::uint8_t *m_Base = nullptr;
  std::size_t m_Size = 0;
  Layout m_Layout;
  const IMAGE_OPTIONAL_HEADER64 *m_Optional = nullptr;
  const IMAGE_SECTION_HEADER *m_Sections = nullptr;
  std::size_t m_SectionCount = 0;
};

static bool IsExecutableProtection(DWORD protection)
{
  if((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
    return false;

  switch(protection & 0xff)
  {
    case PAGE_EXECUTE:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY: return true;
    default: return false;
  }
}

static Finding ClassifyCodeTarget(HMODULE module, const PeView &live,
                                  std::uint32_t rva, const void *&target)
{
  const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(module);
  if(rva > std::numeric_limits<std::uintptr_t>::max() - base)
    return Finding::ExportMalformed;

  target = reinterpret_cast<const void *>(base + rva);
  MEMORY_BASIC_INFORMATION memory = {};
  if(VirtualQuery(target, &memory, sizeof(memory)) != sizeof(memory) ||
     memory.State != MEM_COMMIT)
    return Finding::ExportMalformed;

  const bool executable = IsExecutableProtection(memory.Protect);
  if(memory.Type == MEM_PRIVATE && executable)
    return Finding::EatPointsToPrivateExecutable;

  if(memory.Type == MEM_IMAGE && memory.AllocationBase != module)
    return Finding::EatPointsToOtherImage;

  if(memory.Type != MEM_IMAGE)
    return Finding::Indeterminate;

  if(rva >= live.SizeOfImage())
    return Finding::EatPointsToOtherImage;

  if(!executable || !live.IsExecutableRva(rva))
    return Finding::TargetNotExecutable;

  return Finding::Clean;
}

static bool EndsWithDll(const std::wstring &name)
{
  if(name.size() < 4)
    return false;
  const std::wstring suffix = name.substr(name.size() - 4);
  return _wcsicmp(suffix.c_str(), L".dll") == 0;
}

static bool ParseForwarder(const std::string &text, std::wstring &moduleName, Symbol &symbol)
{
  const std::size_t dot = text.rfind('.');
  if(dot == std::string::npos || dot == 0 || dot + 1 >= text.size())
    return false;

  moduleName.assign(text.begin(), text.begin() + dot);
  if(!EndsWithDll(moduleName))
    moduleName += L".dll";

  const std::string target = text.substr(dot + 1);
  if(target[0] != '#')
  {
    symbol = Symbol::Name(target.c_str());
    return true;
  }

  unsigned long value = 0;
  if(target.size() == 1)
    return false;
  for(std::size_t i = 1; i < target.size(); ++i)
  {
    if(target[i] < '0' || target[i] > '9')
      return false;
    value = value * 10 + static_cast<unsigned long>(target[i] - '0');
    if(value > 0xffff)
      return false;
  }

  symbol = Symbol::Ordinal(static_cast<std::uint16_t>(value));
  return true;
}

static HMODULE DefaultResolveLoadedModule(const wchar_t *moduleName)
{
  return GetModuleHandleW(moduleName); // 查询而不增加引用计数，也不加载新模块
}

struct VisitKey
{
  std::uintptr_t module = 0;
  std::string symbol;

  bool operator<(const VisitKey &other) const
  {
    return module < other.module || (module == other.module && symbol < other.symbol);
  }
};

static Result ValidateExportInternal(HMODULE module, const Symbol &symbol,
                                     const Policy &policy, std::set<VisitKey> &visited,
                                     unsigned depth)
{
  Result result;
  result.module = module;
  result.symbol = symbol;

  if(!module || depth > policy.maxForwardDepth)
  {
    result.finding = Finding::ForwarderCycle;
    return result;
  }

  VisitKey key{reinterpret_cast<std::uintptr_t>(module), symbol.Key()};
  if(!visited.insert(key).second)
  {
    result.finding = Finding::ForwarderCycle;
    return result;
  }

  MODULEINFO moduleInfo = {};
  if(!GetModuleInformation(GetCurrentProcess(), module, &moduleInfo, sizeof(moduleInfo)) ||
     moduleInfo.SizeOfImage == 0)
  {
    result.finding = Finding::ExportMalformed;
    return result;
  }

  MappedFile trustedFile;
  if(!trustedFile.OpenForModule(module, policy))
  {
    result.finding = Finding::ModuleUntrusted;
    return result;
  }

  PeView disk(trustedFile.Data(), trustedFile.Size(), PeView::Layout::RawFile);
  PeView live(module, moduleInfo.SizeOfImage, PeView::Layout::MappedImage);
  if(!disk.Init() || !live.Init())
  {
    result.finding = Finding::ExportMalformed;
    return result;
  }

  const ExportRecord expected = disk.FindExport(symbol);
  const ExportRecord observed = live.FindExport(symbol);
  result.expectedRva = expected.rva;
  result.observedRva = observed.rva;

  if(expected.kind == ExportKind::Malformed || observed.kind == ExportKind::Malformed)
  {
    result.finding = Finding::ExportMalformed;
    return result;
  }
  if(expected.kind == ExportKind::Missing || observed.kind == ExportKind::Missing)
  {
    result.finding = Finding::ExportMissing;
    return result;
  }
  if(expected.kind != observed.kind)
  {
    // 本次攻击可能把原本的 forwarder RVA 直接改成模块外 relay RVA。
    if(observed.kind == ExportKind::CodeRva)
    {
      const Finding targetFinding =
          ClassifyCodeTarget(module, live, observed.rva, result.target);
      if(targetFinding == Finding::EatPointsToPrivateExecutable ||
         targetFinding == Finding::EatPointsToOtherImage)
      {
        result.finding = targetFinding;
        return result;
      }
    }
    result.finding = Finding::ExportKindChanged;
    return result;
  }

  if(expected.kind == ExportKind::Forwarder)
  {
    if(expected.forwarder != observed.forwarder)
    {
      result.finding = Finding::ForwarderChanged;
      return result;
    }

    std::wstring nextModuleName;
    Symbol nextSymbol;
    if(!ParseForwarder(observed.forwarder, nextModuleName, nextSymbol))
    {
      result.finding = Finding::ExportMalformed;
      return result;
    }

    ResolveLoadedModule resolver =
        policy.resolveLoadedModule ? policy.resolveLoadedModule : DefaultResolveLoadedModule;
    HMODULE nextModule = resolver(nextModuleName.c_str());
    if(!nextModule)
    {
      // API-set 名称需要产品接入无副作用的 API-set -> host 解析器；这里绝不 LoadLibrary。
      result.finding = Finding::Indeterminate;
      return result;
    }

    return ValidateExportInternal(nextModule, nextSymbol, policy, visited, depth + 1);
  }

  // 对可信文件也做基本合理性检查。这里的受保护导出都是函数，不是数据导出。
  if(expected.rva >= disk.SizeOfImage() || !disk.IsExecutableRva(expected.rva))
  {
    result.finding = Finding::ExportMalformed;
    return result;
  }

  const Finding targetFinding =
      ClassifyCodeTarget(module, live, observed.rva, result.target);
  if(expected.rva != observed.rva)
  {
    if(targetFinding == Finding::EatPointsToPrivateExecutable ||
       targetFinding == Finding::EatPointsToOtherImage)
      result.finding = targetFinding;
    else
      result.finding = Finding::EatChangedInsideImage;
    return result;
  }

  result.finding = targetFinding;
  return result;
}

Result ValidateExport(HMODULE module, const Symbol &symbol, const Policy &policy)
{
  std::set<VisitKey> visited;
  return ValidateExportInternal(module, symbol, policy, visited, 0);
}

struct ProtectedExport
{
  const wchar_t *moduleName;
  const char *exportName;
};

std::vector<Result> ValidateGraphicsExports(const Policy &policy)
{
  static const ProtectedExport protectedExports[] = {
      {L"dxgi.dll", "CreateDXGIFactory"},
      {L"dxgi.dll", "CreateDXGIFactory1"},
      {L"dxgi.dll", "CreateDXGIFactory2"},
      {L"d3d12.dll", "D3D12CreateDevice"},
      {L"d3d12.dll", "D3D12GetInterface"},
      {L"d3d12.dll", "D3D12GetDebugInterface"},
      {L"D3D12Core.dll", "D3D12CreateDevice"},
      {L"D3D12Core.dll", "D3D12GetInterface"},
      {L"sl.interposer.dll", "D3D12CreateDevice"},
      {L"sl.interposer.dll", "D3D12GetInterface"},
      {L"sl.interposer.dll", "CreateDXGIFactory"},
      {L"sl.interposer.dll", "CreateDXGIFactory1"},
      {L"sl.interposer.dll", "CreateDXGIFactory2"},
  };

  std::vector<Result> results;
  for(const ProtectedExport &item : protectedExports)
  {
    // 不主动加载。不同版本不存在或尚未加载的模块，应由调用时点和版本 manifest 决定
    // 是跳过、等待，还是作为“无法建立信任”处理。
    HMODULE module = GetModuleHandleW(item.moduleName);
    if(module)
      results.push_back(ValidateExport(module, Symbol::Name(item.exportName), policy));
  }
  return results;
}

bool HasCriticalEatRelay(const std::vector<Result> &results)
{
  return std::any_of(results.begin(), results.end(), [](const Result &result) {
    return result.finding == Finding::EatPointsToPrivateExecutable;
  });
}
} // namespace eat_guard
```

### 7.1 `VerifyAgainstProductManifest` 做了什么

该函数已经包含在上面的实现中，依次完成：

1. 从 `pinnedFile` 获取 DOS 最终路径和 NT 最终路径，不信任调用方传入的字符串；
2. 用 `GetMappedFileNameW` 取得 `loadedModule` 的映像来源，并与文件句柄的 NT 路径绑定；
3. 用 `WinVerifyTrust` 对同一个文件句柄执行 Authenticode 验证；
4. 从验证结果中取得最终签名者证书的 SHA-256 指纹；
5. 检查模块文件名和规范化目录是否匹配 manifest；
6. 检查允许的签名证书指纹；
7. 对固定版本文件检查大小和文件 SHA-256；
8. 任一条件无法建立时返回 `false`，最终表现为 `ModuleUntrusted`。

`TrustedModuleRule` 建议按以下方式配置：

| 模块类别 | `allowedRoot` | `expectedSize` | `requireFileSha256` | 签名者规则 |
| --- | --- | --- | --- | --- |
| `dxgi.dll` / `d3d12.dll` | 规范化后的真实 System32 | 通常为 0 | 通常为 false | 当前支持 Windows 版本对应的 Microsoft 证书指纹集合 |
| 游戏自有 DLL | 规范化后的安装目录 | 固定 | true | 游戏发布证书指纹集合 |
| `D3D12Core.dll` | 已批准的 Agility SDK 目录 | 固定 | true | manifest 中批准的签名者和文件 hash |
| `sl.interposer.dll` | 游戏包内批准目录 | 固定 | true | NVIDIA 签名者和该游戏版本的文件 hash |

这里存储的是签名者证书 SHA-256 指纹，不是容易发生本地化或同名问题的 subject 字符串。证书和签名密钥仍可能轮换，因此 manifest 应允许短期并列多个指纹。

`SetVerifiedTrustManifest()` 只接受内存中的不可变规则。调用它之前，产品必须先验证 manifest 自身的数字签名、版本、适用 build ID 和有效期；否则攻击者可以同时替换 DLL 与 manifest。manifest 对象必须在所有校验线程结束前保持存活。

当前映像与文件句柄通过规范化 NT 路径绑定；文件内容还会受 manifest hash 约束。若产品已经有内核反作弊或受信安装服务，可以进一步从该层提供卷 ID、文件 ID 或 section backing-file 身份，以加强对极端文件替换竞争的处理。

如果需要递归处理 `api-ms-win-*` 或 `ext-ms-*` forwarder，还应给 `Policy::resolveLoadedModule` 提供无副作用的 API-set schema 解析器。默认实现只调用 `GetModuleHandleW`，不会因为安全扫描而加载新 DLL；不能解析时返回 `Indeterminate`。

### 7.2 调用示例

```cpp
// 产品实现：读取 manifest 后先验证其签名、build ID、版本和有效期，再生成规则。
eat_guard::TrustedManifest LoadAndVerifySignedGraphicsManifest();
void ReportIntegrityFinding(const eat_guard::Result &result);
void RejectProtectedSession(const char *reasonCode);

// 在启动早期、任何并发校验开始之前调用一次。
void InitializeGraphicsExportIntegrity()
{
  static const eat_guard::TrustedManifest manifest =
      LoadAndVerifySignedGraphicsManifest();
  eat_guard::SetVerifiedTrustManifest(&manifest);
}

void CheckGraphicsExportIntegrity()
{
  eat_guard::Policy policy;
  policy.verifyModuleFile = eat_guard::VerifyAgainstProductManifest;
  policy.resolveLoadedModule = nullptr; // 不需要 API-set 时可使用默认实现

  const std::vector<eat_guard::Result> results =
      eat_guard::ValidateGraphicsExports(policy);

  for(const eat_guard::Result &result : results)
  {
    // 上传结构化 finding；不要上传用户名或完整本地路径。
    ReportIntegrityFinding(result);
  }

  if(eat_guard::HasCriticalEatRelay(results))
  {
    // 不再进入受保护玩法，也不要继续创建新的图形设备。
    RejectProtectedSession("GRAPHICS_EAT_PRIVATE_EXECUTABLE");
  }
}
```

调用示例中仍需产品实现的是 `LoadAndVerifySignedGraphicsManifest`、`ReportIntegrityFinding` 和 `RejectProtectedSession`。实际导出清单也必须按游戏版本维护：某个模块或导出在合法版本中不存在时，应由版本化 manifest 描述，不能把所有 `Missing` 一律解释成攻击。

### 7.3 其他函数的职责

| 函数或类型 | 职责 | 产品通常是否需要修改 |
| --- | --- | --- |
| `MappedFile::OpenForModule` | 打开并固定模块磁盘文件，先执行产品信任校验，再建立只读 raw-file mapping | 否 |
| `PeView` | 同时解析 raw-file layout 和 mapped-image layout，完成边界检查及 EAT/forwarder 读取 | 仅在支持 PE32 时扩展 |
| `HashFileSha256` | 从同一个固定文件句柄计算文件 SHA-256，并恢复文件位置 | 否 |
| `VerifyAuthenticodeAndGetSigner` | 验证 Authenticode 链并返回叶签名证书指纹 | 可能需要调整吊销检查和离线策略 |
| `ClassifyCodeTarget` | 用 `VirtualQuery` 判断目标属于原 `MEM_IMAGE`、其他映像还是私有可执行页 | 否 |
| `ParseForwarder` | 解析 `Module.Export` 和 `Module.#Ordinal` | 否 |
| `ValidateExport` | 比较一个导出的磁盘/内存语义，并递归校验 forwarder | 否 |
| `ValidateGraphicsExports` | 选择本版本需要保护的模块与导出 | 是，应按真实调用链和版本维护 |
| `HasCriticalEatRelay` | 聚合“EAT 指向私有可执行内存”的 Critical finding | 处置策略通常需要扩展 |

`VerifyAuthenticodeAndGetSigner` 当前启用了整条证书链的吊销检查。正式上线前需要明确网络不可用、证书服务器超时和离线启动时的策略，并进行延迟测试；不要因为临时网络问题把大量正常客户端直接作为作弊处罚。可以缓存经过服务端签名且有短有效期的验证结果，但缓存键必须包含文件 ID、hash 和 manifest 版本。

## 8. 检查时点与处置

至少在以下时点执行：

1. Streamline 加载完成后；
2. 第一次 `CreateDXGIFactory*` 之前；
3. 第一次 `D3D12CreateDevice` 之前；
4. 进入受保护线上玩法之前；
5. 运行期间低频、带抖动地复检关键项。

第一次图形调用前的检查最重要：如果等到首帧之后才发现，捕获模块可能已经取得设备和交换链。

实现时最好把“校验”和“解析函数地址”合并成一个原子化程度尽可能高的接口：校验成功后直接返回已经验证的最终地址，调用方不要再执行一次未经验证的 `GetProcAddress` 或手工 EAT 查询。普通代码导出可以返回 `moduleBase + trustedRva`；forwarder 则返回递归验证后的最终地址。这只能缩短检查与使用之间的 TOCTOU 窗口，无法在纯用户态彻底消除竞争，因此仍需要关键时点复检。

推荐处置如下：

| 结果 | 建议 |
| --- | --- |
| EAT 不同且指向模块外 `MEM_PRIVATE + executable` | 阻止进入受保护玩法并记录 Critical finding |
| Forwarder 被修改或指向未批准模块 | 阻止当前受保护会话，要求干净重启 |
| EAT 不同但仍位于本模块映像内 | 记录证据并进一步检查版本、热补丁和代码页 |
| PE 无法安全解析或基线身份不可信 | Fail closed 于受保护玩法，但不要直接永久处罚账号 |
| 仅仅提前加载了 DXGI/D3D12 | 只作为诊断信息，不应处罚 |

检测应返回结构化 finding，不要只返回一个 `bool`。永久处罚需要重复证据、服务端行为或人工复核；本地完整性异常更适合用于会话准入。

## 9. 为什么它能提供防护

它对本次方法有效，原因不是“认识某个捕获工具”，而是锁定了攻击必须破坏的控制流不变量：

```text
关键导出的可信 RVA / forwarder
  -> 必须与已签名文件一致
  -> 最终执行地址必须属于批准模块的 MEM_IMAGE
```

该方案具备以下优点：

- **不依赖名称**：工具、进程和 DLL 改名不会消除 EAT 差异；
- **不受 ASLR 干扰**：比较的是 RVA，而不是每次启动变化的绝对地址；
- **直接命中 relay**：模块外 `MEM_PRIVATE + executable` 正是当前跳板的内存形态；
- **可在使用前拒绝**：在 Device/Factory 创建前检查，可以阻止受篡改进程进入受保护流程；
- **误报相对可控**：合法 forwarder、系统版本差异和模块路径都有明确解析规则。

防护效果来自“检测后拒绝受保护能力”，而不是扫描动作本身。若只写日志但仍继续创建 D3D12 Device，检测不会阻止截帧。

## 10. 明确的能力边界

该检测不是通用反截帧方案，不能直接覆盖：

- EAT 保持不变、但真实函数入口被 inline patch；
- IAT、虚表或运行期函数指针被替换；
- 验证函数本身被 patch，或返回结果被伪造；
- 内核、hypervisor 或 GPU 驱动层捕获；
- 外部录屏或硬件采集。

如果以后需要扩大范围，可以在同一信任模型上增加代码节哈希、IAT 校验和关键函数指针归属检查。但针对本次成功路径，优先把 EAT/forwarder 检测做正确，比一开始进行全进程扫描更准确、成本也更低。

## 11. 最小落地清单

第一版只需完成：

1. 实现带完整边界检查的 raw PE 与 mapped PE 导出解析；
2. 验证系统模块及游戏模块的真实文件身份；
3. 比较关键导出的 RVA、类型和 forwarder 字符串；
4. 用 `VirtualQuery` 验证最终目标的 `Type`、`AllocationBase` 和执行权限；
5. 在第一次 Factory/Device 创建前运行；
6. 将“EAT 差异 + 模块外私有可执行目标”定义为 Critical finding；
7. 建立正常 Windows、驱动、Streamline 和游戏版本的回归样本。

相关格式和 API 定义可参考：

- [PE Format — Export Address Table](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#export-address-table)
- [VirtualQuery](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualquery)
- [WinVerifyTrust](https://learn.microsoft.com/en-us/windows/win32/api/wintrust/nf-wintrust-winverifytrust)

