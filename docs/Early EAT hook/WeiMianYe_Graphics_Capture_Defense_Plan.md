# 《未眠夜》客户端图形捕获防御实施方案

> 一句话总结：针对“挂起态提前加载 DXGI/D3D12，再修改其 EAT”的截帧方式，应在受信启动器创建进程时启用系统缓解策略，并在首个图形设备创建前校验关键模块的来源、签名和 EAT；发现 EAT 指向模块映像之外的私有可执行 relay 时，将其作为高置信度篡改信号交给线上风控处置。

## 1. 目标与边界

本文从防守方视角，针对已验证成功的 RenderDoc 截帧链路提出一套可实施的客户端防御方案。

目标不是承诺“绝对禁止截图或截帧”，而是：

1. 阻断常见的用户态 DLL 注入、DLL 代理和动态 relay；
2. 在 D3D12 Device 创建之前发现 DXGI/D3D12/Streamline 导出表篡改；
3. 防止被篡改客户端进入受保护的线上玩法；
4. 保护未公开资源、Shader、渲染算法和线上公平性；
5. 不因单个弱信号误封正常玩家；
6. 为内部图形调试保留可审计、可撤销的开发通道。

### 1.1 必须承认的安全边界

玩家控制客户端机器时，客户端代码最终必须以明文指令和 GPU 资源形式运行。拥有管理员权限、内核权限、虚拟机监控能力或外接采集设备的对手，原则上总能观察或复制一部分结果。

因此：

- 无法从根本上禁止桌面录屏、驱动层捕获或 HDMI 采集；
- 无法把长期密钥或决定线上胜负的可信状态安全地保存在客户端；
- 客户端完整性检测本身也可能被 patch 或伪造；
- 防御的实际目标是提高攻击成本、缩短未检测窗口，并让服务端不信任异常客户端。

不应把反截帧方案当作服务端权威校验、资源授权或反作弊系统的替代品。

## 2. 需要防御的已知攻击链

本次成功截帧的核心链路可以抽象为：

```text
攻击方启动器
  |
  | CreateProcess(CREATE_SUSPENDED)
  v
游戏主线程尚未运行
  |
  | 远程 LoadLibrary(System32\dxgi.dll)
  | 远程 LoadLibrary(System32\d3d12.dll)
  | 远程 LoadLibrary(捕获模块)
  v
捕获模块初始化
  |
  | 找到 DXGI/D3D12/Streamline 的 EAT
  | 把关键 export RVA 改为近地址 relay
  | relay 绝对跳转到捕获 hook
  v
恢复游戏主线程
  |
  | 游戏或 Streamline 手工读取 EAT
  v
取得捕获 hook，而不是真实 D3D12/DXGI 入口
```

这个链路利用了三个条件：

1. 攻击方掌握进程创建权，可以在游戏初始化前运行远程线程；
2. 游戏信任已加载系统模块的内存状态，没有重新验证 EAT；
3. 进程允许加载任意 DLL，并允许创建新的私有可执行内存。

防御应分别切断这些条件，不能只检测某个工具名称。

## 3. 威胁模型

### 3.1 本方案重点覆盖

- 同权限用户态进程打开游戏句柄；
- `CreateRemoteThread + LoadLibrary` 类注入；
- 创建挂起进程后、主线程执行前的注入；
- 应用目录中的 `dxgi.dll`、`d3d11.dll` 等代理；
- 未授权的 `sl.interposer.dll` 替换；
- 修改 DXGI/D3D12/D3D12Core/Streamline 的 EAT；
- 在系统 DLL 附近分配 `MEM_PRIVATE` 可执行 relay；
- 捕获工具更名后继续使用相同技术。

### 3.2 不应宣称完全覆盖

- 内核驱动或 hypervisor 级对手；
- GPU 驱动内部捕获；
- 硬件采集；
- 已取得游戏代码执行权、并完整伪造检测结果的高级对手；
- 只读取资源而不修改受监控模块的攻击方式。

## 4. 防御总体架构

推荐采用四层架构：

```text
受信启动器
  |
  | 创建时缓解策略 + 启动票据
  v
游戏早期完整性 Bootstrap
  |
  | 安全 DLL 搜索 + 模块/EAT/内存校验
  v
运行期完整性监控
  |
  | 关键时点复检 + 低频抽检 + 证据聚合
  v
线上服务端
  |
  | 风险评分 + 降级/拒绝受保护玩法 + 人工复核
```

四层缺一不可：

- 只有启动器，攻击者仍可独立启动 EXE；
- 只有游戏内检测，攻击者可能先于游戏代码执行；
- 只有本地阻断，攻击者可以 patch 返回值；
- 只有服务端上报，攻击者可以伪造上报。

服务端必须继续通过权威状态、行为检测和资源授权减少对客户端可信度的依赖。

## 5. 第一优先级：在创建进程时启用缓解策略

### 5.1 为什么必须在创建时设置

已知攻击发生在游戏主线程运行之前。因此由游戏 `WinMain` 再调用 `SetProcessMitigationPolicy`，对“主线程前注入”可能已经太晚。

受信启动器应通过 `STARTUPINFOEX` 和 `PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY` 创建游戏，使关键策略在首条游戏指令执行前生效。微软文档说明，这类创建属性指定的策略可在进程启动前应用，并且部分策略在启动后不能放宽：

- [UpdateProcThreadAttribute / PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute)
- [SetProcessMitigationPolicy](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessmitigationpolicy)

### 5.2 推荐策略矩阵

| 策略 | 建议 | 对已知攻击的作用 | 主要兼容风险 |
| --- | --- | --- | --- |
| DEP | 强制开启 | 禁止数据页直接执行 | 现代 x64 游戏通常低风险 |
| CFG | 所有自研二进制启用 | 限制非法间接调用目标 | 第三方 DLL 未启用 CFG 会降低整体效果 |
| Strict CFG | 先审计后决定 | 可拒绝无 CFG 的代码模块 | 可能阻断驱动组件、Overlay、旧中间件 |
| Dynamic Code / ACG | 优先审计，兼容后强制 | 阻止创建或修改动态可执行代码，直接压制当前 relay | JIT、浏览器组件、部分保护壳和中间件可能失效 |
| Extension Point Disable | 建议开启 | 关闭部分历史扩展点，降低 AppInit 等注入面 | 老式输入法、辅助功能或插件需测试 |
| No Remote Images | 建议开启 | 禁止从 UNC 等远程位置加载映像 | 网络盘部署环境需测试 |
| No Low Mandatory Label Images | 建议开启 | 禁止低完整性来源映像 | 下载器/更新器流程需调整 |
| Prefer System32 Images | 建议开启 | 同名系统 DLL 优先 System32 | 依赖应用目录覆盖系统 DLL的程序会受影响 |
| CET User Shadow Stack | 审计后启用 | 增强返回地址和上下文完整性 | 需要硬件、OS 和二进制兼容 |
| Block Non-CET Binaries | 后期可选 | 拒绝不兼容 CET 的模块 | 第三方生态兼容成本较高 |
| MicrosoftSignedOnly/StoreSignedOnly | 不要直接全量启用 | 可强力拒绝普通未签名注入 DLL | 也会拒绝游戏自有及许多合法第三方 DLL |

微软对 ACG 的定义是阻止进程生成动态代码或修改现有可执行代码；当前 EAT 攻击需要在目标模块附近分配可执行 relay，因此 ACG 对这条具体链路很有价值：[PROCESS_MITIGATION_DYNAMIC_CODE_POLICY](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-process_mitigation_dynamic_code_policy)。

但 ACG 不是单独的完整方案：攻击者仍可能加载映像型 DLL、寻找既有代码片段，或换用不需要新建可执行页的方式。

实施 ACG 时应使用不允许 opt-out 的创建策略。不要选择 `PROCESS_CREATION_MITIGATION_POLICY_PROHIBIT_DYNAMIC_CODE_ALWAYS_ON_ALLOW_OPT_OUT`，也不要在后续策略中开启 `AllowThreadOptOut` 或 `AllowRemoteDowngrade`；否则已经进入进程的代码可能获得动态代码豁免或放宽策略。游戏启动后应立即用 `GetProcessMitigationPolicy` 回读实际状态，并把“请求值”和“生效值”同时写入结构化遥测。回读只能发现配置异常，不能替代创建时强制。

CFG 也不能单独阻止本次攻击：它主要约束受 CFG 保护代码的间接调用目标，攻击者仍可能构造直接跳转或跳到合法 CFG 目标。这里应把 CFG 作为通用加固，把 ACG 和 EAT 完整性校验作为针对当前 relay 的直接控制。

### 5.3 建议采用 Audit → Canary → Enforce

不要在所有玩家机器上直接强制最严格策略。建议：

1. **Audit**：收集哪些正常组件触发策略；
2. **Canary**：先覆盖内部、QA 和小比例灰度用户；
3. **Enforce**：只对兼容矩阵已经闭环的策略强制执行；
4. **Rollback**：每项策略都有独立远程开关和版本白名单。

Windows Exploit Protection 支持多种审计模式，可作为兼容性实验参考：[Exploit protection reference](https://learn.microsoft.com/en-us/defender-endpoint/exploit-protection-reference)。

## 6. 第二优先级：收紧 DLL 搜索和组件信任

### 6.1 禁止隐式搜索不可信目录

在进程最早期调用：

```text
SetDefaultDllDirectories(
    LOAD_LIBRARY_SEARCH_SYSTEM32 |
    LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
    LOAD_LIBRARY_SEARCH_USER_DIRS)
```

然后只通过 `AddDllDirectory` 添加产品明确控制的目录，并保存 cookie 以便移除。对于系统 DLL，优先使用：

```text
LoadLibraryEx(..., LOAD_LIBRARY_SEARCH_SYSTEM32)
```

对于随游戏发布的 DLL，使用规范化绝对路径和 `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR`，避免依赖当前工作目录。

微软明确指出，标准 DLL 搜索路径可能受到 DLL preloading 攻击，`SetDefaultDllDirectories` 用于排除易受攻击的目录：[SetDefaultDllDirectories](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-setdefaultdlldirectories)。

这里有两个容易忽略的边界：

- `SetDefaultDllDirectories` 包含 `LOAD_LIBRARY_SEARCH_APPLICATION_DIR` 时，应用目录仍在搜索集合中，而且在默认组合中排在 System32 之前；因此它本身不能保证应用目录里的同名系统 DLL 永远不被加载。敏感系统 DLL 应显式使用 `LOAD_LIBRARY_SEARCH_SYSTEM32`，并结合创建时的 `PreferSystem32Images`。
- 该 API 只能影响调用之后、且走相应搜索规则的动态加载，无法追溯修正 EXE 静态导入阶段已经完成的加载。静态导入需要依赖安全打包、`PreferSystem32Images`、模块路径/签名校验和发布测试共同约束。

因此不要把“调用过 `SetDefaultDllDirectories`”当作验收标准；验收应以“实际加载模块的规范化路径、签名和文件身份正确”为准。

### 6.2 维护签名化组件清单

发布流程生成签名 manifest，至少包含：

- 相对路径；
- 文件大小；
- SHA-256；
- PE timestamp/build ID；
- 允许的签名发布者；
- 是否允许多版本；
- 是否属于系统组件、GPU 厂商组件或游戏自有组件。

manifest 自身必须签名，并由 EXE 内置公钥验证，不能只和文件一起明文放置。

### 6.3 校验 Authenticode，但不要只看“是否有签名”

可通过 `WinVerifyTrust(WINTRUST_ACTION_GENERIC_VERIFY_V2)` 校验 PE 签名。微软说明它可以验证文件来自可信发布者，并确认签名覆盖内容未被修改：[WinVerifyTrust](https://learn.microsoft.com/en-us/windows/win32/api/wintrust/nf-wintrust-winverifytrust)。

实施时还应校验：

- 最终证书链是否符合产品策略；
- signer subject/SPKI 是否在允许集合；
- 时间戳是否有效；
- 文件路径和 NTFS 文件标识是否与验证对象一致；
- 验证完成后实际加载的是否仍是同一个文件。

不能只接受“任意受信 CA 签发的代码签名”，否则攻击者自己的合法签名证书也可能通过。

### 6.4 Streamline 组件

对 `sl.interposer.dll`：

- 使用游戏包内允许版本清单；
- 校验文件 hash 和 NVIDIA 发布者身份；
- 加载后再次确认模块规范化路径；
- 不允许应用目录之外的同名版本；
- 补丁更新必须原子替换，并在启动前完成验证。

这能防止简单的同名代理替换，但不能发现“磁盘文件正常、加载后内存 EAT 被修改”，因此仍需下一节的内存校验。

## 7. 第三优先级：关键模块 EAT 完整性校验

这是对本次成功手法最直接、最有区分度的检测。

### 7.1 不应检测“是否提前加载”

`dxgi.dll` 和 `d3d12.dll` 都是合法系统模块。驱动、Overlay 或其他组件也可能使它们比预期更早加载。因此“模块在 WinMain 前出现”只能作为弱信号，不能用于封禁。

真正应检查的是：

1. 模块是否来自预期路径；
2. 映像是否有可信签名；
3. 内存 EAT 中关键 export RVA 是否仍等于可信磁盘映像；
4. export RVA 是否落在模块 `SizeOfImage` 内；
5. 解析后的地址是否属于该模块的 `MEM_IMAGE` 页面；
6. 是否指向邻近的 `MEM_PRIVATE + executable` relay。

### 7.2 重点模块和导出

第一批最小集合建议为：

```text
dxgi.dll
  CreateDXGIFactory
  CreateDXGIFactory1
  CreateDXGIFactory2

d3d12.dll / d3d12core.dll
  D3D12CreateDevice
  D3D12GetInterface
  D3D12GetDebugInterface
  D3D12EnableExperimentalFeatures

sl.interposer.dll
  CreateDXGIFactory*
  D3D12CreateDevice
  D3D12GetInterface
```

随后再根据实际调用路径扩充根签名、实验特性和其他导出。

### 7.3 建立可信基线

不要通过 `LoadLibrary` 再加载一份“干净 DLL”作为基线，因为加载路径本身可能被 hook，并且会引入初始化副作用。

建议流程：

1. 使用已加载模块句柄取得规范化路径；
2. 以只读方式打开相同文件；
3. 验证文件 ID、签名和路径；
4. 只读映射文件并自行解析 PE；
5. 从磁盘映像中读取 Export Directory；
6. 将关键导出的磁盘 RVA 与已加载映像的内存 EAT RVA 比较。

微软 PE 文档说明 EAT entry 是相对映像基址的 RVA，并给出了 name、ordinal 和 address table 的对应关系：[PE Format — Export Address Table](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#export-address-table)。

### 7.4 需要正确处理 forwarder

如果 EAT entry 指向 Export Directory 自身的范围，它可能不是代码 RVA，而是类似以下形式的 forwarder string：

```text
D3D12Core.D3D12CreateDevice
```

此时应：

- 比较 forwarder string 是否与可信磁盘映像一致；
- 递归验证被转发模块的路径、签名和目标导出；
- 设置最大递归深度并检测循环；
- 不要把合法 forwarder 误判为越界函数地址。

### 7.5 当前攻击的高置信度特征

当前 EAT relay 方案必须让 EAT 保存一个 32 位 RVA。relay 被分配在模块基址之后 4 GiB 内，但通常位于模块的 `SizeOfImage` 之外。

因此以下组合是高置信度篡改证据：

```text
memory EAT RVA != clean file EAT RVA
AND
moduleBase + memory EAT RVA is outside module SizeOfImage
AND
VirtualQuery(target).Type == MEM_PRIVATE
AND
target page is executable
AND
relay ultimately transfers control to an unapproved image
```

单独出现 `MEM_PRIVATE + executable` 不足以封禁，因为 JIT、浏览器、保护壳或某些中间件也可能合法使用动态代码；但它与关键系统 EAT 差异组合时，区分度显著提高。

### 7.6 检查时点

至少在以下时点执行：

1. 早期 Bootstrap 初始化；
2. 加载 Streamline 之后；
3. 第一次 `D3D12CreateDevice` 之前；
4. 第一次创建 DXGI Factory 之前；
5. 进入受保护线上玩法之前；
6. 运行中以低频、带抖动的方式抽检。

“只在启动时检查一次”不够，因为攻击者可以等检查结束后再修改 EAT。

### 7.7 推荐的数据结构

```cpp
struct ProtectedExport
{
  ModuleIdentity module;
  std::string exportName;
  uint32_t cleanRva;
  ExportKind kind;        // CodeRva or Forwarder
};

struct IntegrityFinding
{
  FindingType type;
  ModuleIdentity module;
  std::string exportName;
  uint32_t expectedRva;
  uint32_t observedRva;
  MemoryClassification targetMemory;
  Confidence confidence;
};
```

`ModuleIdentity` 不应只有 DLL 名称，还应包括规范化路径、卷标识、文件 ID、签名发布者和 build/version。

### 7.8 示例校验流程

以下是设计级伪代码，不依赖具体反作弊产品：

```text
ValidateProtectedExport(module, exportName):
    identity = ResolveAndVerifyModuleIdentity(module)
    if identity is not allowed:
        return UntrustedModule

    clean = ParseExportFromVerifiedFile(identity.openFileHandle, exportName)
    live  = ParseExportFromLoadedImage(module.base, exportName)

    if clean.kind != live.kind:
        return ExportKindChanged

    if clean.kind == Forwarder:
        if clean.forwarderString != live.forwarderString:
            return ForwarderChanged
        return ValidateForwardTarget(live.forwarderString)

    if clean.rva != live.rva:
        target = module.base + live.rva
        memory = VirtualQuery(target)
        return EATChanged(clean.rva, live.rva, memory)

    if live.rva >= module.sizeOfImage:
        return ExportOutsideImage

    memory = VirtualQuery(module.base + live.rva)
    if memory.Type != MEM_IMAGE or memory.AllocationBase != module.base:
        return ExportTargetNotInOwningImage

    return Clean
```

所有 PE 边界、RVA 加法和表大小都必须做溢出检查，避免完整性扫描器本身成为恶意 PE 的攻击面。

## 8. 第四优先级：可执行私有内存与 relay 检测

### 8.1 进程地址空间分类

通过 `VirtualQuery` 枚举虚拟地址空间，记录：

- `MEM_IMAGE + executable`：正常 PE 代码的主要形态；
- `MEM_PRIVATE + executable`：动态生成代码或注入 relay 的候选；
- `MEM_MAPPED + executable`：需结合来源判断；
- 同时可写、可执行的页面：高风险配置。

优先检查：

- DXGI/D3D12/D3D12Core 映像前后 4 GiB 范围；
- 被 EAT、IAT 或关键函数指针引用的页面；
- 在游戏启动早期出现的小型可执行私有 allocation；
- 内容呈现为短跳板，且目标落入未授权 DLL 的页面。

不要在每个检查点线性扫描完整的 4 GiB 邻近范围。首选路径是从受保护 EAT、IAT 和函数指针反向查询实际目标页；如需建立全局内存地图，可在后台低频增量维护，并设置页数、耗时和解析深度上限。任何 relay 指令解码都应在受控缓冲区中完成，失败时返回“不确定”，不能让畸形指令导致游戏崩溃。

### 8.2 不要把字节签名作为唯一依据

relay 的指令可以从 `jmp [rip+0]` 改成 `mov/jmp`、`push/ret` 或其他等价形式。防御应基于控制流与内存归属，而不是只搜索某组固定字节。

### 8.3 与 ACG 配合

ACG 能从源头阻止许多动态可执行页；运行期扫描则负责：

- 发现因兼容原因未开启 ACG 的机器；
- 发现策略被降级或未成功应用；
- 发现攻击者使用已有映像代码而非新建 relay 的变体。

## 9. 第五优先级：模块和加载事件监控

### 9.1 模块允许策略

模块分为四类：

1. Windows 系统模块；
2. GPU 厂商及 Streamline 模块；
3. 游戏自有、已签名模块；
4. 明确允许的 Overlay、输入法、辅助功能和反作弊组件。

对其他模块记录：

- 完整规范化路径；
- 文件签名与发布者；
- 文件 hash 和版本；
- 首次出现时间；
- 映像是否在首帧前加载；
- 是否被关键 EAT/IAT/函数指针引用。

### 9.2 名称检测只能作为弱信号

不要以 `renderdoc.dll`、窗口标题、日志名、控制端口或进程名作为主要策略。工具可以轻易改名，本次案例本身已经证明名称并不是稳定安全边界。

这些信号可用于 QA 诊断，但不能单独触发封禁。

### 9.3 远程线程和句柄

进程自身可以收紧部分对象 ACL，外部安全服务也可以观察可疑句柄和线程创建。但对于“创建游戏进程的父进程已经持有全权限句柄”的情况，游戏内部再修改 DACL 已经无法撤回父进程现有句柄。

因此：

- 创建时策略比启动后 DACL 更重要；
- 高对抗线上模式可集成成熟反作弊产品的服务/驱动能力；
- 不建议项目自行快速开发内核拦截驱动，错误实现会扩大系统攻击面并带来隐私、签名、兼容和运维风险。

## 10. 启动器与线上启动票据

### 10.1 受信启动器职责

启动器应：

1. 验证自身更新状态；
2. 验证游戏 EXE 和签名 manifest；
3. 使用扩展启动属性创建游戏；
4. 设置创建时缓解策略；
5. 只继承必需句柄；
6. 向游戏传递一次性启动票据；
7. 将启动策略版本纳入遥测。

### 10.2 启动票据设计

服务端签发短期一次性票据，绑定：

- 账号与会话；
- 游戏 build ID；
- manifest version；
- 随机 nonce；
- 过期时间；
- 启动策略版本；
- 线上环境标识。

游戏进入受保护玩法前向服务端兑现票据。直接双击 EXE 可以允许离线或非保护模式，但不能进入需要完整性保证的在线模式。

票据不能证明客户端永远未被修改，但能消除“完全绕过官方启动器仍无条件进入线上”的低成本路径。

票据也不能单独证明“这次进程一定由官方启动器以指定缓解策略创建”：本机攻击者可能转移票据、模拟 IPC 或 patch 客户端校验。实现时应让启动器先与服务端完成认证挑战，再通过最小权限继承句柄或受限本地 IPC 把一次性材料交给子进程，并绑定 PID、创建时间、build ID 和服务端 nonce；这些绑定仍然只是提高重放和转移成本，最终处置必须结合服务端行为与客户端完整性证据。

## 11. 客户端处置策略

### 11.1 不要根据单一弱信号永久封禁

建议使用证据聚合：

| 信号 | 建议置信度 |
| --- | --- |
| 关键 EAT 与可信文件不一致，目标为模块外 `MEM_PRIVATE + RX` | Critical |
| 关键 EAT 指向未授权映像 | Critical |
| 创建时缓解策略缺失或被降级 | High |
| 游戏/Streamline 组件签名或 hash 不匹配 | High |
| 首帧前出现未知未签名 DLL | High |
| 可执行私有页，但未被关键控制流引用 | Medium |
| DXGI/D3D12 提前加载 | Low |
| 工具名称、窗口、日志或端口命中 | Low |

### 11.2 推荐响应

**离线模式：**

- 可以警告并关闭受保护资源或联网功能；
- 不应故意破坏用户文件；
- 给出可诊断错误码，而不是静默崩溃。

**线上普通玩法：**

- Critical 信号阻止进入对局；
- High 信号要求重启并重新验证；
- Medium/Low 信号只上报和观察。

**竞技或高价值玩法：**

- 要求启动票据和完整缓解策略；
- Critical 信号立即结束受保护会话；
- 永久处罚需要服务端行为证据、重复命中或人工复核。

### 11.3 遥测最小化

只上传安全判断所需字段，例如：

- build ID；
- finding type；
- 模块发布者、版本和 hash 前缀；
- expected/observed RVA；
- 内存类型和保护属性；
- 策略状态位；
- 匿名化设备/会话标识。

默认不要上传完整本地路径、命令行、用户名或内存内容。遥测格式、保留周期和用户告知应经过隐私与法务评审。

## 12. 服务端与内容保护

### 12.1 服务端权威

以下信息不能依赖客户端保密：

- 战斗判定；
- 掉落和经济系统；
- 匹配与排名；
- 反作弊规则核心状态；
- 长期服务密钥；
- 未授权内容的永久解密密钥。

客户端即使完全阻止 RenderDoc，也不代表这些数据安全。

### 12.2 资源保护

对未公开内容可采用：

- 按版本和账号授权下载；
- 分包和按需下发；
- 短期内容密钥；
- 水印或可追踪的资源变体；
- 发布前环境与生产环境分离；
- Shader symbol、调试字符串和编辑器元数据清理。

资源加密只能保护静态存储和传输；资源提交给 GPU 时仍会以可使用形式存在，不能宣称其能永久阻止提取。

## 13. 内部调试与合作方工作流

完全禁止图形调试会显著伤害研发效率。建议保留独立的 capture-enabled 通道：

```text
Production build
  - 默认启用完整性策略
  - 不接受调试豁免
  - 只连接生产服务

Capture-enabled development build
  - 明确水印
  - 使用不同签名或 build ID
  - 允许 RenderDoc/PIX/Nsight
  - 禁止连接生产竞技服务
```

不要使用可伪造的环境变量或普通配置项作为生产版豁免开关。可使用签名调试许可，但生产服务端仍应拒绝 capture-enabled build。

合作方需要抓帧时，提供：

- 限时、可撤销的签名开发包；
- 指定账号和测试环境；
- 明确的资源与日志脱敏规范；
- 捕获文件的保管、传输和销毁要求。

## 14. 分阶段实施计划

### 阶段 0：建立基线（1～2 周）

- 枚举生产环境所有合法 DLL；
- 记录 DXGI/D3D12/D3D12Core/Streamline 正常 EAT；
- 收集 Windows 10/11、不同 GPU 和驱动的差异；
- 建立 PIX、Nsight、Steam、Discord、GeForce Experience、OBS 等兼容样本；
- 所有检测仅记录，不处置。

交付物：

- 模块清单；
- 签名策略；
- 关键 export 清单；
- 误报基线；
- 安全遥测 schema。

### 阶段 1：安全加载（2～4 周）

- 接入 `SetDefaultDllDirectories`；
- 所有敏感 DLL 改为绝对路径或限定搜索 flag；
- 建立签名 manifest；
- 对 Streamline 和游戏自有 DLL 做启动前验证；
- 修复更新器的非原子替换流程。

验收：应用目录同名代理不能被意外加载，篡改 Streamline 文件能在加载前得到明确错误。

### 阶段 2：EAT 完整性 MVP（2～4 周）

- 实现安全 PE parser 或复用经过审计的内部组件；
- 校验最小关键 export 集合；
- 处理 forwarder、Agility SDK 和不同系统版本；
- 在首个 Device/Factory 创建前执行；
- 输出结构化 finding，不直接封禁。

验收：当前已知 EAT relay 方案应在首次 D3D12 Device 创建前命中 Critical finding。

### 阶段 3：创建时缓解策略（3～6 周）

- 启动器改用 `STARTUPINFOEX`；
- 开启 DEP、基础 CFG、Extension Point Disable、Image Load Policy；
- 对 ACG、Strict CFG、CET 分别做 audit/canary；
- 建立按 OS/GPU/驱动/中间件版本的远程策略矩阵。

验收：在官方启动器且 ACG 确实生效的兼容机器上，当前攻击所需的私有可执行 relay 无法建立；不兼容机器可安全回退并保留告警。回退状态不得被误报为“已强制保护”。

### 阶段 4：运行期监控和服务端闭环（3～6 周）

- 关键时点复检 EAT；
- 扫描关键控制流引用的可执行私有页；
- 启动票据接入线上服务；
- 实现风险评分、对局准入和申诉证据；
- 完成隐私评审与运营流程。

### 阶段 5：高对抗模式（按业务需要）

- 评估成熟反作弊供应商；
- 只在竞技/高价值玩法启用更强策略；
- 不自行仓促实现内核驱动；
- 建立驱动签名、崩溃率、性能、隐私和紧急回滚 SLA。

## 15. 测试矩阵

### 15.1 必测正常场景

- Windows 10/11 各受支持版本；
- NVIDIA、AMD、Intel 多代 GPU；
- D3D12 Agility SDK 各生产版本；
- DLSS、Frame Generation、Reflex；
- 全屏、无边框、窗口模式；
- Steam/Discord/GeForce/AMD Overlay；
- OBS/Game Bar 等正常录屏；
- 输入法、屏幕阅读器和辅助功能；
- 崩溃收集器、性能分析器和客服诊断工具；
- 安装、升级、回滚和修复。

### 15.2 必测攻击回归

- 创建挂起进程后远程加载 DXGI/D3D12；
- 主线程前注入未授权 DLL；
- 修改 `D3D12CreateDevice` EAT；
- 修改 `CreateDXGIFactory*` EAT；
- EAT 指向模块外 `MEM_PRIVATE + RX` relay；
- EAT 指向另一个未授权 `MEM_IMAGE`；
- 应用目录放置同名 DXGI/D3D11 DLL；
- 替换 `sl.interposer.dll`；
- 检查后再延迟修改 EAT；
- 工具和模块全部改名；
- 删除或伪造本地检测日志。

### 15.3 验收标准

- 当前已知截帧链路在首个 D3D12 Device 前被阻断或标记；
- 正常玩家误报率满足业务阈值；
- 不因未知模块直接造成永久封禁；
- 安全扫描不显著增加启动时间或帧时间尖峰；
- 所有强制策略均可按版本远程回滚；远程配置必须签名、设有效期，并保留不可远程降级的最低安全基线；
- capture-enabled 开发包仍能完成日常图形调试。

## 16. 推荐的最小可行版本

如果资源有限，优先实现以下五项：

1. 启动器使用创建时 mitigation policy；
2. 收紧 DLL 搜索路径，并验证 Streamline/游戏 DLL 签名和 hash；
3. 在首个 `D3D12CreateDevice` 前比较关键 EAT 与可信磁盘映像；
4. 将“EAT 差异 + 模块外 MEM_PRIVATE 可执行目标”作为 Critical finding；
5. 线上受保护玩法要求官方启动票据，并根据聚合证据拒绝异常会话。

这五项直接覆盖本次成功路径，同时避免依赖工具名称和脆弱字节特征。

## 17. 最终建议

本次经验反向说明，防御重点不应是“检查 RenderDoc 是否存在”，而应是保护图形 API 的信任链：

```text
可信启动器
  -> 创建时缓解策略
  -> 受控 DLL 搜索与签名验证
  -> 可信的 DXGI/D3D12/Streamline 映像
  -> 未被修改的 EAT
  -> 关键调用只落入批准的 MEM_IMAGE
  -> 服务端仅接纳满足完整性要求的会话
```

对于当前已知方法，最直接的技术判据是：

> 关键系统模块的 EAT 是否与可信磁盘映像一致；如果不一致，新的目标地址是否落在模块映像之外的私有可执行内存中。

应把这一判据与 ACG、CFG、安全 DLL 加载、签名 manifest 和服务端准入结合使用。任何单点用户态检测都可能被绕过，但分层防御能够显著提高稳定截帧和规模化滥用的成本。
