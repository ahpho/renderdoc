# 《未眠野》RenderDoc 截帧适配技术回顾

> 一句话总结：在普通 Launch 创建的挂起进程中提前加载 DXGI/D3D12，并在游戏或 Streamline 读取函数地址前修改其 EAT，将图形 API 调用重定向到 RenderDoc hook，从而实现正常截帧。

## 1. 文档目的

本文回顾 `GameClient-Win64-Shipping.exe` 的 RenderDoc 截帧适配过程，重点说明：

- 为什么最初能够注入 `renderdoc.dll`、建立 GUI 控制连接，却仍然无法截帧；
- 为什么 Global Hook、DXGI/D3D11 代理和替换 `sl.interposer.dll` 的方案没有解决问题；
- 最终普通 Launch 方案为什么有效；
- EAT hook、近地址 relay、挂起态预加载分别解决了什么问题；
- 当前实现对其他游戏的影响范围；
- 本次未提交修改中仍然存在的工程风险和后续改进建议。

本文描述的是当前工作区中的实现，代码尚未提交。

## 2. 最终结果

最终可工作的入口是普通 **Launch**，不是 Global Hook。

成功运行的直接产物为：

```text
<本地目录>\RenderDoc\工具在这里\《未眠野》PC版\
GameClient-Win64-Shipping_2026.09.28_12.05.58_frame1625.rdc
```

该文件大小为 `285,580,946` 字节，说明目标 D3D12 设备、交换链和帧事件已经进入 RenderDoc 的捕获链路，而不仅仅是完成了 DLL 注入或 GUI 握手。

最终方案具有以下特征：

- 目标进程通过 RenderDoc 原生 Launch 流程创建，并保持主线程挂起；
- 在注入 `renderdoc.dll` 前，注入器先把系统 `dxgi.dll`、`d3d12.dll` 加载到目标进程；
- `renderdoc.dll` 初始化时立即扫描这些已加载模块，并修改其导出地址表；
- NVIDIA Streamline 或游戏自己的解析器即使手工遍历导出表，也只能取得 RenderDoc relay 的地址；
- 不替换游戏的 `sl.interposer.dll`；
- 不把 `dxgi.dll`、`d3d11.dll` 或 `renderdoc.dll` 复制到游戏目录；
- 不主动加载 `d3d12core.dll`，避免干扰 D3D12 Agility SDK 的运行时选择。

## 3. 最初现象为什么具有迷惑性

最初可以观察到：

1. `renderdoc.dll` 已经进入游戏进程；
2. `renderdoc.dll` 创建了控制台窗口；
3. RenderDoc GUI 可以发现或连接目标进程；
4. DXGI Factory 的部分创建调用能够进入 RenderDoc；
5. 但 Capture Frame 按钮仍不可用，F12 不生成 `.rdc`。

这些现象并不矛盾，因为 RenderDoc 的“进程控制链路”和“图形 API 捕获链路”是两套不同的机制。

```text
进程控制链路：
renderdoc.dll -> Target Control Socket -> qrenderdoc GUI

图形捕获链路：
D3D12CreateDevice -> WrappedID3D12Device
                  -> WrappedIDXGIFactory / Wrapped SwapChain
                  -> Present / TriggerCapture
                  -> .rdc
```

只要 `renderdoc.dll` 初始化成功，目标控制端口就可以建立。因此 GUI 能连接目标，只能证明注入成功，不能证明 D3D12 Device 已被包装。

旧日志中的关键信号是：

```text
Creating swap chain with non-hooked device
This means the swapchain will BYPASS RenderDoc hook. Capture will FAIL!
```

这表示 DXGI Factory 已经进入 RenderDoc，但传入 Factory 的 D3D12 Device 是原始设备，不是 `WrappedID3D12Device`。在这种状态下，交换链无法进入正常捕获路径，Capture Frame 和 F12 自然不会产生有效帧。

## 4. 普通 RenderDoc Hook 为什么被绕过

### 4.1 IAT hook

常规 Windows 应用通过 PE Import Address Table（IAT）调用外部 DLL：

```text
Game IAT entry -> d3d12!D3D12CreateDevice
```

RenderDoc 扫描已加载模块，将 IAT entry 改为自己的 hook：

```text
Game IAT entry -> RenderDoc hook -> real D3D12CreateDevice
```

### 4.2 GetProcAddress hook

对于动态加载的函数，RenderDoc 还会 hook `LoadLibrary*` 和 `GetProcAddress`：

```text
GetProcAddress(d3d12, "D3D12CreateDevice") -> RenderDoc hook address
```

### 4.3 手工遍历 EAT

本目标的关键差异是，Streamline 或私有加载路径可以直接读取模块 PE 头，定位 Export Address Table（EAT），然后自行计算函数地址：

```text
module base
  + IMAGE_EXPORT_DIRECTORY.AddressOfFunctions[ordinal]
  = raw function address
```

这种路径既不读取游戏 IAT，也不调用被 RenderDoc 替换的 `GetProcAddress`，因此两个传统拦截点都会被绕过。

一旦游戏在 RenderDoc 完成 hook 之前缓存了原始 `D3D12CreateDevice` 地址，后续再修改 IAT 或 `GetProcAddress` 已经没有意义。游戏会始终调用缓存的原始地址，从而创建未包装的 D3D12 Device。

## 5. 曾经尝试的方案及失败原因

### 5.1 DXGI/D3D11 本地代理

将 `dxgi.dll` 或 `d3d11.dll` 放在游戏目录，可以利用 Windows DLL 搜索顺序取得较早执行机会。这种技术适合传统加载路径，但存在几个限制：

- 游戏可能安全加载系统 DLL，不接受应用目录代理；
- 真正的问题发生在 D3D12 Device 创建，而不只是 DXGI Factory 创建；
- 即使 Factory 被代理，传入的 Device 仍可能是未包装设备；
- 游戏更新或反篡改机制可能拒绝本地代理 DLL。

因此该方案可以证明部分 DXGI 调用路径，但不能保证 D3D12 Device 进入 RenderDoc。

### 5.2 替换 `sl.interposer.dll`

代理 `sl.interposer.dll` 的思路是把 Streamline 导出的 D3D12/DXGI 函数转发到常规系统 API，让 RenderDoc 的既有 hook 有机会介入。

该方案在技术上可行，但本目标使用了 Streamline 安全加载逻辑。原版 `sl.interposer.dll` 带 NVIDIA 数字签名，而本地编译的代理没有等价签名。游戏包含 `StreamlineSecureLoad` 相关逻辑，因此代理可能在进入自身 `DllMain` 之前就被拒绝。

这解释了以下现象：

- 文件替换和备份操作在 GUI 日志里成功；
- 代理自己的日志却完全没有创建；
- `renderdoc.dll` 也没有通过代理进入进程；
- 游戏可能直接中止该加载分支，而不是执行代理转发逻辑。

### 5.3 Global Hook

Global Hook 主要依赖 AppInit/系统级注入入口，适合不能由 GUI 直接启动的进程，但它不保证本目标在图形入口解析前完成注入。

本次运行中没有看到目标 PID、目标控制握手或 RenderDoc 图形初始化日志，说明 Global Hook 没有进入有效链路。即使 Global Hook 最终能够注入，它也不能天然解决“游戏已经缓存原始导出地址”的时序问题。

### 5.4 只增加 EAT hook，但安装得太晚

EAT hook 可以处理手工导出表解析，但前提是 EAT 必须在游戏读取它之前被修改。

此前日志的时间顺序显示，`dxgi.dll`、`d3d12.dll`、`d3d12core.dll` 和 `sl.interposer.dll` 的补丁安装存在明显延迟。游戏有机会先解析并缓存真实函数地址，随后 EAT 再被修改也无法追回已经缓存的指针。

因此最终问题不只是“hook 哪张表”，还包括“何时 hook”。

## 6. 最终方案的完整时序

普通 Launch 的关键优势是 `RunProcess` 使用 `CREATE_SUSPENDED` 创建目标进程。目标主线程尚未执行游戏代码，注入器拥有一个确定的早期窗口。

当前流程如下：

```text
qrenderdoc
  |
  | CreateProcessW(CREATE_SUSPENDED)
  v
GameClient-Win64-Shipping.exe
  primary thread: suspended
  |
  | remote LoadLibraryW(C:\Windows\System32\dxgi.dll)
  | remote LoadLibraryW(C:\Windows\System32\d3d12.dll)
  |
  | remote LoadLibraryW(renderdoc.dll)
  v
renderdoc.dll!DllMain
  |
  | Register D3D12/DXGI hooks
  | HookAllModules()
  | Patch IAT + GetProcAddress path + EAT
  v
Return to injector
  |
  | ResumeThread(primary thread)
  v
Game / Streamline starts resolving graphics exports
  |
  | EAT already points to RenderDoc relay
  v
Wrapped D3D12 Device -> Wrapped SwapChain -> capture succeeds
```

核心不是简单地“提前加载 DLL”，而是把这两个动作组合起来：

1. 在游戏主线程恢复前，让目标图形入口模块确定地出现在模块列表中；
2. 在同一个挂起窗口内注入 RenderDoc，并立即修改这些模块的 EAT。

这消除了此前的模块加载竞态。

## 7. 为什么只预加载 `dxgi.dll` 和 `d3d12.dll`

### 7.1 使用 System32 绝对路径

注入器通过 `GetSystemDirectoryW` 构造绝对路径，再远程调用 `LoadLibraryW`。这样不会因为游戏目录、当前目录或 PATH 中的同名 DLL 而误加载代理版本。

### 7.2 不主动加载 `sl.interposer.dll`

`sl.interposer.dll` 属于游戏/Streamline 组件，主动加载会改变其预期初始化顺序，并重新引入签名、安全加载和 loader-lock 风险。

最终方案不需要替换或提前加载它。因为 Streamline 后续解析 `dxgi.dll` 或 `d3d12.dll` 的 EAT 时，系统模块的目标导出已经被修改。

### 7.3 不主动加载 `d3d12core.dll`

现代 D3D12 程序可能使用 Agility SDK。实际使用的 `d3d12core.dll` 版本和路径由 D3D12 loader、应用导出或 SDK 配置决定。

如果 RenderDoc 在游戏之前强行从 System32 或任意固定位置加载 `d3d12core.dll`，可能选错运行时版本。当前实现只预加载稳定的系统入口 `d3d12.dll`，让 Windows/D3D12 loader 按应用自己的配置选择 Core。

第一次 `HookAllModules()` 在解析 D3D12 转发导出时可能触发正确的 `d3d12core.dll` 加载，因此目标模式下会执行第二次模块扫描，补上新出现模块的 EAT hook。

## 8. EAT hook 的实现细节

### 8.1 定位导出项

实现依次读取：

```text
IMAGE_DOS_HEADER
  -> IMAGE_NT_HEADERS
  -> OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT]
  -> IMAGE_EXPORT_DIRECTORY
```

然后使用：

- `AddressOfNames` 找到导出名称；
- `AddressOfNameOrdinals` 将名称映射到 ordinal；
- `AddressOfFunctions[ordinal]` 找到需要修改的 EAT entry。

只有已经注册在 RenderDoc `FunctionHooks` 中的函数才会被修改，不会无差别重定向模块的全部导出。

当前目标模块集合为：

- `dxgi.dll`；
- `d3d12.dll`；
- `d3d12core.dll`；
- `sl.interposer.dll`。

### 8.2 先保存真实入口

在改写 EAT 前，RenderDoc 通过原始 `GetProcAddress` 填充对应 `hook.orig`。

因此调用链为：

```text
caller
  -> patched EAT
  -> relay
  -> RenderDoc hook body
  -> hook.orig
  -> real implementation
```

如果先改 EAT、后获取 `hook.orig`，`GetProcAddress` 可能再次返回 relay，最终形成递归。因此初始化顺序是正确性的必要条件。

### 8.3 为什么不能直接把 hook 地址写进 EAT

PE EAT 的函数项不是完整指针，而是相对模块基址的 32 位 RVA：

```text
function address = module base + uint32 RVA
```

在 64 位进程中，`renderdoc.dll` 的 hook 地址和系统 `d3d12.dll` 的模块基址可能相距超过 4 GiB，无法直接编码成合法 RVA。

当前实现会在被 hook 模块基址之后、4 GiB 可达范围内扫描空闲地址区，并分配一块 relay page：

```text
EAT RVA -> nearby relay -> absolute jump -> RenderDoc hook
```

每个 relay 使用固定 stride。x64 relay 的核心指令为：

```text
ENDBR64
jmp qword ptr [rip+0]
<absolute 64-bit hook address>
```

`ENDBR64`/`ENDBR32` 用于兼容 CET 间接分支跟踪；间接绝对跳转避免破坏参数寄存器和调用约定。

relay 写入完成后，页面权限从可写改为 `PAGE_EXECUTE_READ`，并刷新指令缓存。EAT entry 则临时切换为可写，记录原始 RVA 后写入 relay RVA。

### 8.4 恢复逻辑

当前代码保存：

- `EAT entry address -> original RVA`；
- 每个 relay block 的地址和大小。

移除 hook 时先恢复所有原始 EAT RVA，成功后释放 relay。若任何 EAT 无法恢复，代码会保留 relay 内存，避免仍指向 relay 的导出项变成悬空执行地址。

## 9. 目标识别与其他游戏隔离

成功方案不是对所有程序全局启用。它包含三层独立判断：

| 层级 | 判断位置 | 条件 |
| --- | --- | --- |
| GUI 模式 | `CaptureDialog.cpp` | 文件名为 `GameClient-Win64-Shipping.exe`，且安装根或路径包含 `WeiMianYe` 标记 |
| 启动器预加载 | `win32_process.cpp` | 同一 EXE 名，且规范化路径包含 `\\weimianye\\` 或 `\\weimianyegame\\` |
| 目标进程 EAT hook | `win32_hook.cpp` | 从目标进程自身路径再次检查同一 EXE 名和安装标记 |

因此，对其他游戏的结论是：

- 其他普通 EXE：不会提前加载 DXGI/D3D12，也不会启用 EAT hook；
- 其他同名 Unreal Engine Shipping EXE：只要路径不含上述安装标记，也不会启用；
- 当前目标：Global Hook 会被 UI 阻止，要求使用普通 Launch；
- `yysls.exe`：仍保留旧的 `FullGraphicsProxy` 模式，这是一条独立的兼容路径；

EAT 状态容器虽然存在于所有目标中，但在目标判断失败时保持为空；目标外的 `EndHookRegistration()` 也不会执行额外的第二次扫描。

## 10. Code review 结论

### 10.1 高风险：旧代理清理可能删除不属于 RenderDoc 的文件

`ProxyCaptureUninstallExtraFiles()` 会按文件名直接删除：

```text
d3d11.dll
dxgi.dll
renderdoc.dll
```

它没有部署清单、hash、文件版本或来源校验。当前目标每次普通 Launch 都会调用旧代理清理，因此如果游戏更新后合法地携带同名 DLL，或者用户手工放置了其他代理，这些文件也会被删除。

该问题因目标路径判断而不会波及无关游戏，但可能破坏当前目标和旧 `yysls.exe` 代理目标的文件完整性。

建议：

- 安装时写入 manifest，记录目标路径、原文件 hash、代理 hash和备份路径；
- 只删除 hash 与已部署代理一致的文件；
- 对已有同名文件先做唯一备份，不能直接覆盖；
- 清理异常时保留文件并给出人工处理提示。

### 10.2 高风险：Streamline 代理安装失败可能留下缺失的原 DLL

安装流程先把真实 `sl.interposer.dll` 重命名为 `slinterposerorig.dll`，随后复制代理。

如果重命名成功但复制代理失败，失败目录尚未加入外层 `done` 列表，因此外层 rollback 不会恢复该目录。最终可能出现：

```text
sl.interposer.dll      missing
slinterposerorig.dll   present
```

建议让单目录安装函数自身具备事务性：复制到临时文件、校验、原子替换；任何一步失败都必须在返回前恢复原名。

### 10.3 中风险：控制台日志开关对所有游戏生效

`OUTPUT_LOG_TO_PRINTF` 是编译期全局宏。打开后并不只影响当前目标。结合此 fork 已有的强制控制台创建逻辑，任何加载 `renderdoc.dll` 的游戏都可能出现额外控制台和大量 RenderDoc 输出。

可能的副作用包括：

- 改变无控制台 GUI 游戏的窗口行为；
- 污染命令行程序的 stdout/stderr；
- 增加高频日志开销；
- 改变部分启动器或反篡改模块观察到的进程行为。

建议改为 `debug.ini` 或目标 predicate 控制，而不是长期保持全局编译期开关。

### 10.4 中风险：构建路径硬编码且清理脚本掩盖失败

`qrenderdoc_local.vcxproj` 将 Release x64 安装目录硬编码为：

```text
<安装目录>\RenderDoc\
```

这会影响其他开发机、其他 checkout 和 CI。新增 cleanup 脚本还会始终 `exit /b 0`，即使 `del` 失败也报告成功；由于它是 PostBuild 的最后一条命令，还可能掩盖前一个 `_post_build.cmd` 的非零返回码。

建议：

- 使用 MSBuild property 或本机 `.user` 配置指定安装目录；
- 每个 `call` 后立即检查 `errorlevel`；
- 删除后验证根目录中的代理确实不存在；
- 失败时让 PostBuild 返回非零结果。

### 10.5 中风险：EAT hook 没有 DLL 卸载通知

`s_InstalledExportHooks` 使用 EAT entry 的裸地址作为 key。若被修改模块卸载：

- map 中会保留指向已卸载映像的地址；
- 同模块重载时，旧记录可能阻止重新安装；
- `RemoveHooks()` 只能在 `VirtualProtect` 失败后保守泄漏 relay；
- 多份同名模块的 `hook.orig` 仍以主模块为准，不一定与触发调用的副本一致。

预加载的系统 `dxgi.dll`、`d3d12.dll` 因远程 `LoadLibrary` 引用通常会保持到进程退出，所以成功路径的主要模块风险较低；`d3d12core.dll` 和 `sl.interposer.dll` 仍可能受动态卸载影响。

建议接入 loader notification，按模块基址记录 hook，并在 unload 时注销相应 EAT entry 和 relay 所有权。

### 10.6 中风险：预加载失败不会让 Launch 失败

`PreloadEarlyExportHookModules()` 当前只记录错误，不返回状态。即使 DXGI 或 D3D12 预加载失败，代码仍会注入 RenderDoc、恢复主线程并返回目标控制连接。

这会重新产生“GUI 看似连接成功但无法截帧”的模糊状态。

建议返回结构化结果，并在本目标的必要模块预加载失败时中止 Launch，向 GUI 显示具体 Win32 错误。

### 10.7 低风险：目标 predicate 重复实现

目标识别分别以 Qt、`rdcstr` 和目标进程 ANSI 路径实现。当前三处规则一致，但未来只修改其中一处会导致：

- GUI 显示普通模式，注入器却没有预加载；
- 注入器预加载成功，目标内部却没有启用 EAT hook；
- 路径 Unicode、分隔符或命名变化造成不一致。

建议将模式作为显式 capture option 或环境/注入参数传给目标，而不是让三个组件分别猜测。

## 11. 哪些修改会影响其他游戏

| 修改 | 其他游戏运行时影响 | 说明 |
| --- | --- | --- |
| 挂起态预加载 DXGI/D3D12 | 无直接影响 | 有 EXE 名和安装路径双重限制 |
| EAT hook/relay | 无直接影响 | 目标进程内再次校验；其他进程不会写 EAT |
| 第二次 `HookAllModules()` | 无直接影响 | 仅目标模式执行 |
| 阻止 Global Hook | 无直接影响 | 只对当前目标模式生效 |
| `FullGraphicsProxy` | 仅旧 `yysls.exe` | 按 EXE 名匹配的遗留兼容路径 |
| wildcard shim | Global Hook 范围内 | 本次主要是技术性重命名，原分支已启用 wildcard 行为 |
| `OUTPUT_LOG_TO_PRINTF` | **有** | 编译期全局开关，所有注入目标均受影响 |
| PostBuild 硬编码和代理清理 | **有构建侧影响** | 影响所有使用该工程文件的开发环境 |

总体结论：最终成功的捕获技术本身对其他游戏隔离良好；需要优先治理的跨游戏副作用是全局日志/控制台行为。文件部署与回滚风险虽然被路径规则限制，但破坏性更强，也应在继续维护旧代理模式前修复。

## 12. 推荐验证矩阵

后续修改建议至少覆盖以下测试：

1. 当前目标普通 Launch：能连接、Capture Frame 可用、F12 生成 `.rdc`；
2. 当前目标 Global Hook：UI 明确拒绝并提示使用 Launch；
3. 任意其他 D3D11 游戏：不预加载 D3D12，不启用 EAT hook；
4. 任意其他 D3D12 游戏：保持上游 RenderDoc 行为；
5. 另一个名为 `GameClient-Win64-Shipping.exe` 的 UE 游戏：路径不含标记时不得启用特殊逻辑；
6. 当前目标路径使用 `/`、`\\`、大小写变化时，三层 predicate 结果一致；
7. 模拟 `dxgi.dll`/`d3d12.dll` 预加载失败：GUI 应得到明确失败原因；
8. 模拟代理复制失败：真实 `sl.interposer.dll` 必须自动恢复；
9. 目标目录已有非 RenderDoc 的 `dxgi.dll`：清理流程不得删除；
10. 普通游戏注入：确认是否仍需要控制台和全量 printf 日志。

## 13. 后续收敛建议

建议把当前实现收敛为两条清晰分离的路径：

```text
LegacyProxyCapture
  - 仅维护确实需要代理的旧目标
  - 带 manifest、hash 和事务式回滚
  - 不在 DllMain 做重初始化

EarlyModuleExportHooks
  - 普通 Launch
  - 挂起态预加载系统入口模块
  - 目标限定的 EAT relay
  - 不修改游戏安装文件
```

对当前目标而言，`EarlyModuleExportHooks` 已经证明足够。DXGI、D3D11 和 Streamline 代理不再是成功路径的依赖，后续应避免把遗留代理逻辑与当前目标重新耦合。

## 14. 总结

本次问题的本质不是“RenderDoc 没有注入”，而是：

> 目标在 RenderDoc 修改传统调用入口之前，通过手工导出表解析取得并缓存了真实 D3D12 函数地址。

最终成功依赖两个缺一不可的改动：

1. 使用普通 Launch 的挂起进程窗口，提前加载系统 DXGI/D3D12 模块；
2. 修改模块 EAT，并通过 4 GiB 可达 relay 把手工解析者导向 RenderDoc hook。

这样既保留了游戏原版、已签名的 Streamline 组件，也避免了代理 DLL 搜索顺序、安全加载和 Global Hook 时序的不确定性。最终 `.rdc` 的生成证明，D3D12 Device 已从“注入成功但未包装”的状态进入完整 RenderDoc 捕获链路。
