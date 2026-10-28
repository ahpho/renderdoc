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

#include <shellapi.h>
#include <winsock2.h>
#include "core/core.h"
#include "hooks/hooks.h"
#include "os/os_specific.h"
#include "strings/string_utils.h"

#include <string>

typedef int(WSAAPI *PFN_WSASTARTUP)(__in WORD wVersionRequested, __out LPWSADATA lpWSAData);
typedef int(WSAAPI *PFN_WSACLEANUP)();

typedef BOOL(WINAPI *PFN_CREATE_PROCESS_A)(LPCSTR lpApplicationName, LPSTR lpCommandLine,
                                           LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                           LPSECURITY_ATTRIBUTES lpThreadAttributes,
                                           BOOL bInheritHandles, DWORD dwCreationFlags,
                                           LPVOID lpEnvironment, LPCSTR lpCurrentDirectory,
                                           LPSTARTUPINFOA lpStartupInfo,
                                           LPPROCESS_INFORMATION lpProcessInformation);

typedef BOOL(WINAPI *PFN_CREATE_PROCESS_W)(LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
                                           LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                           LPSECURITY_ATTRIBUTES lpThreadAttributes,
                                           BOOL bInheritHandles, DWORD dwCreationFlags,
                                           LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
                                           LPSTARTUPINFOW lpStartupInfo,
                                           LPPROCESS_INFORMATION lpProcessInformation);

typedef BOOL(WINAPI *PFN_CREATE_PROCESS_AS_USER_A)(
    HANDLE hToken, LPCSTR lpApplicationName, LPSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCSTR lpCurrentDirectory,
    LPSTARTUPINFOA lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation);

typedef BOOL(WINAPI *PFN_CREATE_PROCESS_AS_USER_W)(
    HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
    LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation);

typedef BOOL(WINAPI *PFN_CREATE_PROCESS_WITH_LOGON_W)(LPCWSTR lpUsername, LPCWSTR lpDomain,
                                                      LPCWSTR lpPassword, DWORD dwLogonFlags,
                                                      LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
                                                      DWORD dwCreationFlags, LPVOID lpEnvironment,
                                                      LPCWSTR lpCurrentDirectory,
                                                      LPSTARTUPINFOW lpStartupInfo,
                                                      LPPROCESS_INFORMATION lpProcessInformation);

typedef HINSTANCE(WINAPI *PFN_SHELL_EXECUTE_A)(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile,
                                               LPCSTR lpParameters, LPCSTR lpDirectory, INT nShowCmd);
typedef HINSTANCE(WINAPI *PFN_SHELL_EXECUTE_W)(HWND hwnd, LPCWSTR lpOperation, LPCWSTR lpFile,
                                               LPCWSTR lpParameters, LPCWSTR lpDirectory,
                                               INT nShowCmd);
typedef BOOL(WINAPI *PFN_SHELL_EXECUTE_EX_A)(LPSHELLEXECUTEINFOA lpExecInfo);
typedef BOOL(WINAPI *PFN_SHELL_EXECUTE_EX_W)(LPSHELLEXECUTEINFOW lpExecInfo);

class SysHook : LibraryHook
{
public:
  SysHook()
  {
    m_nameLibraryHook = "SysHook";

    // we start with a refcount of 1 because we initialise WSA ourselves for our own sockets.
    m_WSARefCount = 1;
  }

  void RegisterHooks()
  {
    RDCLOG("Registering Win32 system hooks");

    // register libraries that we care about. We don't need a callback when they are loaded
    LibraryHooks::RegisterLibraryHook("kernel32.dll", NULL);
    LibraryHooks::RegisterLibraryHook("advapi32.dll", NULL);
    LibraryHooks::RegisterLibraryHook("api-ms-win-core-processthreads-l1-1-0.dll", NULL);
    LibraryHooks::RegisterLibraryHook("api-ms-win-core-processthreads-l1-1-1.dll", NULL);
    LibraryHooks::RegisterLibraryHook("api-ms-win-core-processthreads-l1-1-2.dll", NULL);
    LibraryHooks::RegisterLibraryHook("ws2_32.dll", NULL);

    // we want to hook CreateProcess purely so that we can recursively insert our hooks (if we so
    // wish)
    CreateProcessA.Register("kernel32.dll", "CreateProcessA", CreateProcessA_hook);
    CreateProcessW.Register("kernel32.dll", "CreateProcessW", CreateProcessW_hook);
    CreateProcessAsUserA.Register("advapi32.dll", "CreateProcessAsUserA", CreateProcessAsUserA_hook);
    CreateProcessAsUserW.Register("advapi32.dll", "CreateProcessAsUserW", CreateProcessAsUserW_hook);

    CreateProcessWithLogonW.Register("advapi32.dll", "CreateProcessWithLogonW",
                                     CreateProcessWithLogonW_hook);

    // The shell creates processes inside its own implementation, so the hooks above never see a
    // launcher that hands off through ShellExecute. Hook those entry points too, so that child
    // hooking also covers shell-launched processes.
    LibraryHooks::RegisterLibraryHook("shell32.dll", NULL);
    ShellExecuteA.Register("shell32.dll", "ShellExecuteA", ShellExecuteA_hook);
    ShellExecuteW.Register("shell32.dll", "ShellExecuteW", ShellExecuteW_hook);
    ShellExecuteExA.Register("shell32.dll", "ShellExecuteExA", ShellExecuteExA_hook);
    ShellExecuteExW.Register("shell32.dll", "ShellExecuteExW", ShellExecuteExW_hook);

    // handle API set exports if they exist. These don't really exist so we don't have to worry
    // about double hooking, and also they call into the 'real' implementation in kernelbase.dll
    API110CreateProcessA.Register("api-ms-win-core-processthreads-l1-1-0.dll", "CreateProcessA",
                                  API110CreateProcessA_hook);
    API110CreateProcessW.Register("api-ms-win-core-processthreads-l1-1-0.dll", "CreateProcessW",
                                  API110CreateProcessW_hook);
    API110CreateProcessAsUserW.Register("api-ms-win-core-processthreads-l1-1-0.dll",
                                        "CreateProcessAsUserW", API110CreateProcessAsUserW_hook);

    API111CreateProcessA.Register("api-ms-win-core-processthreads-l1-1-1.dll", "CreateProcessA",
                                  API111CreateProcessA_hook);
    API111CreateProcessW.Register("api-ms-win-core-processthreads-l1-1-1.dll", "CreateProcessW",
                                  API111CreateProcessW_hook);
    API111CreateProcessAsUserW.Register("api-ms-win-core-processthreads-l1-1-0.dll",
                                        "CreateProcessAsUserW", API111CreateProcessAsUserW_hook);

    API112CreateProcessA.Register("api-ms-win-core-processthreads-l1-1-2.dll", "CreateProcessA",
                                  API112CreateProcessA_hook);
    API112CreateProcessW.Register("api-ms-win-core-processthreads-l1-1-2.dll", "CreateProcessW",
                                  API112CreateProcessW_hook);
    API112CreateProcessAsUserW.Register("api-ms-win-core-processthreads-l1-1-0.dll",
                                        "CreateProcessAsUserW", API112CreateProcessAsUserW_hook);

    WSAStartup.Register("ws2_32.dll", "WSAStartup", WSAStartup_hook);
    WSACleanup.Register("ws2_32.dll", "WSACleanup", WSACleanup_hook);

    m_RecurseSlot = Threading::AllocateTLSSlot();
    Threading::SetTLSValue(m_RecurseSlot, NULL);

    RDCLOG("Registering Win32 system hooks ends.");
  }

private:
  static SysHook syshooks;

  int m_WSARefCount;
  uint64_t m_RecurseSlot = 0;

  bool CheckRecurse()
  {
    if(Threading::GetTLSValue(m_RecurseSlot) == NULL)
    {
      Threading::SetTLSValue(m_RecurseSlot, (void *)1);
      return false;
    }

    return true;
  }
  void EndRecurse() { Threading::SetTLSValue(m_RecurseSlot, NULL); }
  HookedFunction<PFN_CREATE_PROCESS_A> CreateProcessA;
  HookedFunction<PFN_CREATE_PROCESS_W> CreateProcessW;

  HookedFunction<PFN_CREATE_PROCESS_A> API110CreateProcessA;
  HookedFunction<PFN_CREATE_PROCESS_W> API110CreateProcessW;
  HookedFunction<PFN_CREATE_PROCESS_A> API111CreateProcessA;
  HookedFunction<PFN_CREATE_PROCESS_W> API111CreateProcessW;
  HookedFunction<PFN_CREATE_PROCESS_A> API112CreateProcessA;
  HookedFunction<PFN_CREATE_PROCESS_W> API112CreateProcessW;

  HookedFunction<PFN_CREATE_PROCESS_AS_USER_A> CreateProcessAsUserA;
  HookedFunction<PFN_CREATE_PROCESS_AS_USER_W> CreateProcessAsUserW;

  HookedFunction<PFN_CREATE_PROCESS_AS_USER_W> API110CreateProcessAsUserW;
  HookedFunction<PFN_CREATE_PROCESS_AS_USER_W> API111CreateProcessAsUserW;
  HookedFunction<PFN_CREATE_PROCESS_AS_USER_W> API112CreateProcessAsUserW;

  HookedFunction<PFN_CREATE_PROCESS_WITH_LOGON_W> CreateProcessWithLogonW;

  HookedFunction<PFN_SHELL_EXECUTE_A> ShellExecuteA;
  HookedFunction<PFN_SHELL_EXECUTE_W> ShellExecuteW;
  HookedFunction<PFN_SHELL_EXECUTE_EX_A> ShellExecuteExA;
  HookedFunction<PFN_SHELL_EXECUTE_EX_W> ShellExecuteExW;

  HookedFunction<PFN_WSASTARTUP> WSAStartup;
  HookedFunction<PFN_WSACLEANUP> WSACleanup;

  static int WSAAPI WSAStartup_hook(WORD wVersionRequested, LPWSADATA lpWSAData)
  {
    int ret = syshooks.WSAStartup()(wVersionRequested, lpWSAData);

    // only increment the refcount if the function succeeded
    if(ret == 0)
      syshooks.m_WSARefCount++;

    return ret;
  }

  static int WSAAPI WSACleanup_hook()
  {
    // don't let the application murder our sockets with a mismatched WSACleanup() call
    if(syshooks.m_WSARefCount == 1)
    {
      RDCLOG("WSACleanup called with (to the application) no WSAStartup! Ignoring.");
      SetLastError(WSANOTINITIALISED);
      return SOCKET_ERROR;
    }

    // decrement refcount and call the real thing
    syshooks.m_WSARefCount--;
    return syshooks.WSACleanup()();
  }

  static BOOL WINAPI
  Hooked_CreateProcess(const char *entryPoint,
                       std::function<BOOL(DWORD dwCreationFlags, LPVOID pEnvironment,
                                          LPPROCESS_INFORMATION lpProcessInformation)>
                           realFunc,
                       DWORD dwCreationFlags, bool inject, LPVOID pEnvironment,
                       LPPROCESS_INFORMATION lpProcessInformation, const rdcstr &appDesc)
  {
    bool recursive = syshooks.CheckRecurse();

    if(recursive)
      return realFunc(dwCreationFlags, pEnvironment, lpProcessInformation);

    PROCESS_INFORMATION dummy;
    RDCEraseEl(dummy);

    // not sure if this is valid, but I need the PID so I'll fill in my own struct to ensure that.
    if(lpProcessInformation == NULL)
    {
      lpProcessInformation = &dummy;
    }
    else
    {
      *lpProcessInformation = dummy;
    }

    bool resume = (dwCreationFlags & CREATE_SUSPENDED) == 0;
    dwCreationFlags |= CREATE_SUSPENDED;

    rdcstr envA;
    std::wstring envW;
    void *env = pEnvironment;
    const bool unicode_env = (dwCreationFlags & CREATE_UNICODE_ENVIRONMENT) != 0;

// give ourselves access to the ANSI version if we want it
#undef GetEnvironmentStrings

    static_assert(std::is_same<decltype(GetEnvironmentStrings()), char *>::value,
                  "GetEnvironmentStrings macro is messing up");

    // if we have no existing environment, take it from the current env strings that will be used
    // implicitly so we can patch it
    if(!env)
      env = unicode_env ? (void *)GetEnvironmentStringsW() : (void *)GetEnvironmentStrings();

    // patch the environment string to remove vulkan layer variable
    if(unicode_env)
    {
      const wchar_t *cur = (const wchar_t *)env;

      // loop over every A=B\0 string
      while(*cur)
      {
        // if it is NOT the vulkan env var, append it to our block
        if(wcsncmp(cur, CONCAT(L, RENDERDOC_VULKAN_LAYER_VAR), sizeof(RENDERDOC_VULKAN_LAYER_VAR) - 1))
        {
          envW += cur;
          envW.push_back(L'\0');
        }

        cur += wcslen(cur) + 1;
      }

      // append the extra \0 to terminate the block
      envW.push_back(L'\0');

      // use the patched block
      env = (void *)envW.data();
    }
    else
    {
      const char *cur = (const char *)env;

      // loop over every A=B\0 string
      while(*cur)
      {
        // if it is NOT the vulkan env var, append it to our block
        if(strncmp(cur, RENDERDOC_VULKAN_LAYER_VAR, sizeof(RENDERDOC_VULKAN_LAYER_VAR) - 1))
        {
          envA += cur;
          envA.push_back('\0');
        }

        cur += strlen(cur) + 1;
      }

      // append the extra \0 to terminate the block
      envA.push_back('\0');

      // use the patched block
      env = (void *)envA.data();
    }

    RDCDEBUG("Calling real %s for %s", entryPoint, appDesc.c_str());
    BOOL ret = realFunc(dwCreationFlags, env, lpProcessInformation);
    RDCDEBUG("Called real %s for %s (PID=%u)", entryPoint, appDesc.c_str(),
             lpProcessInformation->dwProcessId);

    if(ret && inject)
    {
      RDCDEBUG("Intercepting %s", entryPoint);

      // inherit logfile and capture options
      rdcpair<RDResult, uint32_t> res = Process::InjectIntoProcess(
          lpProcessInformation->dwProcessId, {}, RenderDoc::Inst().GetCaptureFileTemplate(),
          RenderDoc::Inst().GetCaptureOptions(), false);

      if(res.first == ResultCode::Succeeded)
        RenderDoc::Inst().AddChildProcess((uint32_t)lpProcessInformation->dwProcessId, res.second);
    }

    if(resume)
    {
      ResumeThread(lpProcessInformation->hThread);
    }

    // ensure we clean up after ourselves
    if(dummy.dwProcessId != 0)
    {
      CloseHandle(dummy.hProcess);
      CloseHandle(dummy.hThread);
    }

    syshooks.EndRecurse();

    return ret;
  }

  // Some children must never be hooked even when child hooking is enabled:
  //  - anti-cheat binaries, which may treat any foreign module in themselves as an attack
  //  - Qt WebEngine/Chromium renderers, whose sandbox rejects the injection and which launchers
  //    only use for their own UI
  //  - Unity's crash handler, which would then also try to capture
  static bool IsExcludedChild(const rdcstr &path)
  {
    if(path.empty())
      return false;

    const char *name = path.c_str();
    for(const char *c = name; *c; c++)
    {
      if(*c == '\\' || *c == '/')
        name = c + 1;
    }

    rdcstr lower = strlower(rdcstr(name));

    // all of the AntiCheatExpert executables share this prefix, including the ones that are not
    // listed explicitly
    if(lower.beginsWith("ace-"))
      return true;

    return lower == "qtwebengineprocess.exe" || lower == "unitycrashhandler64.exe" ||
           lower == "unitycrashhandler32.exe";
  }

  // ASCII-only case-insensitive comparison, to avoid pulling in towlower from the CRT
  static bool LowerEquals(wchar_t c, wchar_t lower)
  {
    return c == lower || c == wchar_t(lower - L'a' + L'A');
  }

  static bool ShouldInject(LPCWSTR lpApplicationName, LPCWSTR lpCommandLine)
  {
    if(!RenderDoc::Inst().GetCaptureOptions().hookIntoChildren)
      return false;

    bool inject = true;

    // sanity check to make sure we're not going to go into an infinity loop injecting into
    // ourselves.
    if(lpApplicationName)
    {
      rdcstr app = strlower(StringFormat::Wide2UTF8(lpApplicationName));

      if(app.contains(STRINGIZE(RDOC_BASE_NAME) "cmd.exe") ||
                      app.contains("q" STRINGIZE(RDOC_BASE_NAME) ".exe"))
      {
        inject = false;
      }
    }
    if(lpCommandLine)
    {
      rdcstr cmd = strlower(StringFormat::Wide2UTF8(lpCommandLine));

      if(cmd.contains(STRINGIZE(RDOC_BASE_NAME) "cmd.exe") ||
                      cmd.contains("q" STRINGIZE(RDOC_BASE_NAME) ".exe"))
      {
        inject = false;
      }
    }

    if(inject)
    {
      // the child's executable is either the application name, or the first token of the command
      // line (which may be quoted)
      rdcstr child = lpApplicationName ? StringFormat::Wide2UTF8(lpApplicationName) : rdcstr();

      if(child.empty() && lpCommandLine && lpCommandLine[0])
      {
        const wchar_t *start = lpCommandLine;
        if(*start == L'"')
          start++;

        const wchar_t *end = NULL;
        if(*lpCommandLine == L'"')
        {
          end = start;
          while(*end && *end != L'"')
            end++;
        }
        else
        {
          // the path is not quoted, so it may contain spaces. Take everything up to the
          // executable extension rather than stopping at the first space, which would turn
          // "C:\\Program Files\\Game\\ace-setup64.exe" into "C:\\Program".
          for(const wchar_t *p = start; *p; p++)
          {
            if(p[0] == L'.' && p[1] && p[2] && p[3] && LowerEquals(p[1], L'e') &&
               LowerEquals(p[2], L'x') && LowerEquals(p[3], L'e'))
            {
              const wchar_t next = p[4];
              if(next == 0 || next == L' ' || next == L'\t' || next == L'"')
              {
                end = p + 4;
                break;
              }
            }
          }

          // no extension, fall back to the first token
          if(end == NULL)
          {
            end = start;
            while(*end && *end != L' ')
              end++;
          }
        }

        child = StringFormat::Wide2UTF8(rdcwstr(start, end - start));
      }

      if(IsExcludedChild(child))
      {
        RDCDEBUG("Not hooking excluded child %s", child.c_str());
        inject = false;
      }
    }

    return inject;
  }

  static bool ShouldInject(LPCSTR lpApplicationName, LPCSTR lpCommandLine)
  {
    if(!RenderDoc::Inst().GetCaptureOptions().hookIntoChildren)
      return false;

    return ShouldInject(lpApplicationName ? StringFormat::UTF82Wide(lpApplicationName).c_str() : NULL,
                        lpCommandLine ? StringFormat::UTF82Wide(lpCommandLine).c_str() : NULL);
  }

  static bool ProcessIsElevated()
  {
    static int cached = -1;

    if(cached < 0)
    {
      cached = 0;

      HANDLE token = NULL;
      if(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
      {
        TOKEN_ELEVATION elevation = {};
        DWORD size = sizeof(elevation);

        if(GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size))
          cached = elevation.TokenIsElevated ? 1 : 0;

        CloseHandle(token);
      }
    }

    return cached == 1;
  }

  // True when a shell call is a plain "run this program" request that we can handle ourselves.
  // Other verbs are left to the real shell, except "runas" which we can honour when we are already
  // elevated, otherwise the shell has to show the elevation prompt.
  static bool IsShellExecuteOperation(LPCWSTR lpOperation)
  {
    if(lpOperation == NULL || lpOperation[0] == 0)
      return true;

    if(_wcsicmp(lpOperation, L"open") == 0)
      return true;

    if(_wcsicmp(lpOperation, L"runas") == 0)
      return ProcessIsElevated();

    return false;
  }

  // Resolve what the shell was asked to run to an existing .exe. A relative path is resolved
  // against the directory passed to ShellExecute, since that is what the shell would do.
  // Documents, URLs and directories are rejected and left to the real shell.
  static bool ResolveShellExecuteTarget(LPCWSTR lpFile, LPCWSTR lpDirectory, std::wstring &outPath)
  {
    if(lpFile == NULL || lpFile[0] == 0)
      return false;

    // don't touch URLs or shell namespace objects
    if(wcsstr(lpFile, L"://") != NULL)
      return false;

    auto isExecutable = [&outPath](const std::wstring &candidate) -> bool {
      wchar_t full[1024];
      const DWORD len = GetFullPathNameW(candidate.c_str(), ARRAY_COUNT(full), full, NULL);

      if(len == 0 || len >= ARRAY_COUNT(full))
        return false;

      const size_t nameLen = wcslen(full);

      if(nameLen < 4 || !LowerEquals(full[nameLen - 4], L'.') ||
         !LowerEquals(full[nameLen - 3], L'e') || !LowerEquals(full[nameLen - 2], L'x') ||
         !LowerEquals(full[nameLen - 1], L'e'))
        return false;

      const DWORD attrs = GetFileAttributesW(full);

      if(attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
        return false;

      outPath = full;
      return true;
    };

    if(isExecutable(lpFile))
      return true;

    if(lpDirectory && lpDirectory[0])
      return isExecutable(std::wstring(lpDirectory) + L"\\" + lpFile);

    return false;
  }

  // Run the executable the shell was asked to launch via CreateProcess, so that it goes through
  // the same suspend/inject/resume path as any other child process.
  static BOOL RunShellExecuteTarget(LPCWSTR lpFile, LPCWSTR lpParameters, LPCWSTR lpDirectory,
                                    int nShowCmd, PROCESS_INFORMATION *outPi)
  {
    std::wstring cmdline = L"\"";
    cmdline += lpFile;
    cmdline += L"\"";

    if(lpParameters && lpParameters[0])
    {
      cmdline += L" ";
      cmdline += lpParameters;
    }

    STARTUPINFOW si;
    RDCEraseEl(si);
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = (WORD)nShowCmd;

    PROCESS_INFORMATION pi;
    RDCEraseEl(pi);

    // CreateProcessW may modify the command line, so it needs a writable buffer
    std::wstring mutableCmd = cmdline;

    const BOOL ret = Hooked_CreateProcess(
        "CreateProcessW",
        [&](DWORD flags, LPVOID env, LPPROCESS_INFORMATION out) {
          return syshooks.CreateProcessW()(lpFile, &mutableCmd[0], NULL, NULL, FALSE, flags, env,
                                           lpDirectory, &si, out);
        },
        0, ShouldInject(lpFile, cmdline.c_str()), NULL, &pi, GetProcessDesc(lpFile, cmdline.c_str()));

    if(outPi)
      *outPi = pi;

    return ret;
  }

  static HINSTANCE WINAPI ShellExecuteW_hook(HWND hwnd, LPCWSTR lpOperation, LPCWSTR lpFile,
                                             LPCWSTR lpParameters, LPCWSTR lpDirectory, INT nShowCmd)
  {
    std::wstring target;

    if(RenderDoc::Inst().GetCaptureOptions().hookIntoChildren &&
       IsShellExecuteOperation(lpOperation) && ResolveShellExecuteTarget(lpFile, lpDirectory, target))
    {
      PROCESS_INFORMATION pi;

      if(RunShellExecuteTarget(target.c_str(), lpParameters, lpDirectory, nShowCmd, &pi))
      {
        RDCLOG("ShellExecuteW: ran '%s' directly so that it can be injected",
               StringFormat::Wide2UTF8(target.c_str()).c_str());

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        // success, reported the same way the shell would
        return (HINSTANCE)42;
      }
    }

    return syshooks.ShellExecuteW()(hwnd, lpOperation, lpFile, lpParameters, lpDirectory, nShowCmd);
  }

  static HINSTANCE WINAPI ShellExecuteA_hook(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile,
                                             LPCSTR lpParameters, LPCSTR lpDirectory, INT nShowCmd)
  {
    std::wstring target;

    if(RenderDoc::Inst().GetCaptureOptions().hookIntoChildren && lpFile)
    {
      const std::wstring operation = lpOperation ? StringFormat::UTF82Wide(lpOperation).c_str() : L"";
      const std::wstring file = StringFormat::UTF82Wide(lpFile).c_str();
      const std::wstring parameters =
          lpParameters ? StringFormat::UTF82Wide(lpParameters).c_str() : L"";
      const std::wstring directory = lpDirectory ? StringFormat::UTF82Wide(lpDirectory).c_str() : L"";

      if(IsShellExecuteOperation(operation.empty() ? NULL : operation.c_str()) &&
         ResolveShellExecuteTarget(file.c_str(), directory.empty() ? NULL : directory.c_str(), target))
      {
        PROCESS_INFORMATION pi;

        if(RunShellExecuteTarget(target.c_str(), parameters.empty() ? NULL : parameters.c_str(),
                                 directory.empty() ? NULL : directory.c_str(), nShowCmd, &pi))
        {
          RDCLOG("ShellExecuteA: ran '%s' directly so that it can be injected", lpFile);

          CloseHandle(pi.hProcess);
          CloseHandle(pi.hThread);

          return (HINSTANCE)42;
        }
      }
    }

    return syshooks.ShellExecuteA()(hwnd, lpOperation, lpFile, lpParameters, lpDirectory, nShowCmd);
  }

  // flags that mean the shell has to do more than just create a process
  static const UINT ShellExecuteExUnsupportedFlags = SEE_MASK_CLASSNAME | SEE_MASK_CLASSKEY |
                                                     SEE_MASK_IDLIST | SEE_MASK_INVOKEIDLIST |
                                                     SEE_MASK_HOTKEY | SEE_MASK_HMONITOR;

  static BOOL WINAPI ShellExecuteExW_hook(LPSHELLEXECUTEINFOW lpExecInfo)
  {
    std::wstring target;

    if(lpExecInfo && lpExecInfo->cbSize == sizeof(SHELLEXECUTEINFOW) &&
       RenderDoc::Inst().GetCaptureOptions().hookIntoChildren &&
       (lpExecInfo->fMask & ShellExecuteExUnsupportedFlags) == 0 &&
       IsShellExecuteOperation(lpExecInfo->lpVerb) &&
       ResolveShellExecuteTarget(lpExecInfo->lpFile, lpExecInfo->lpDirectory, target))
    {
      PROCESS_INFORMATION pi;

      if(RunShellExecuteTarget(target.c_str(), lpExecInfo->lpParameters, lpExecInfo->lpDirectory,
                               lpExecInfo->nShow, &pi))
      {
        RDCLOG("ShellExecuteExW: ran '%s' directly so that it can be injected",
               StringFormat::Wide2UTF8(target.c_str()).c_str());

        if(lpExecInfo->fMask & SEE_MASK_NOCLOSEPROCESS)
        {
          lpExecInfo->hProcess = pi.hProcess;
          CloseHandle(pi.hThread);
        }
        else
        {
          CloseHandle(pi.hProcess);
          CloseHandle(pi.hThread);
        }

        lpExecInfo->hInstApp = (HINSTANCE)42;
        return TRUE;
      }
    }

    return syshooks.ShellExecuteExW()(lpExecInfo);
  }

  static BOOL WINAPI ShellExecuteExA_hook(LPSHELLEXECUTEINFOA lpExecInfo)
  {
    std::wstring target;

    if(lpExecInfo && lpExecInfo->cbSize == sizeof(SHELLEXECUTEINFOA) &&
       RenderDoc::Inst().GetCaptureOptions().hookIntoChildren &&
       (lpExecInfo->fMask & ShellExecuteExUnsupportedFlags) == 0 && lpExecInfo->lpFile)
    {
      const std::wstring operation =
          lpExecInfo->lpVerb ? StringFormat::UTF82Wide(lpExecInfo->lpVerb).c_str() : L"";
      const std::wstring file = StringFormat::UTF82Wide(lpExecInfo->lpFile).c_str();
      const std::wstring parameters =
          lpExecInfo->lpParameters ? StringFormat::UTF82Wide(lpExecInfo->lpParameters).c_str() : L"";
      const std::wstring directory =
          lpExecInfo->lpDirectory ? StringFormat::UTF82Wide(lpExecInfo->lpDirectory).c_str() : L"";

      if(IsShellExecuteOperation(operation.empty() ? NULL : operation.c_str()) &&
         ResolveShellExecuteTarget(file.c_str(), directory.empty() ? NULL : directory.c_str(), target))
      {
        PROCESS_INFORMATION pi;

        if(RunShellExecuteTarget(target.c_str(), parameters.empty() ? NULL : parameters.c_str(),
                                 directory.empty() ? NULL : directory.c_str(), lpExecInfo->nShow, &pi))
        {
          RDCLOG("ShellExecuteExA: ran '%s' directly so that it can be injected", lpExecInfo->lpFile);

          if(lpExecInfo->fMask & SEE_MASK_NOCLOSEPROCESS)
          {
            lpExecInfo->hProcess = pi.hProcess;
            CloseHandle(pi.hThread);
          }
          else
          {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
          }

          lpExecInfo->hInstApp = (HINSTANCE)42;
          return TRUE;
        }
      }
    }

    return syshooks.ShellExecuteExA()(lpExecInfo);
  }

  static rdcstr GetProcessDesc(LPCWSTR lpApplicationName, LPCWSTR lpCommandLine)
  {
    if(lpApplicationName)
      return StringFormat::Wide2UTF8(lpApplicationName);
    if(lpCommandLine)
      return StringFormat::Wide2UTF8(lpCommandLine);
    return "<unknown>";
  }

  static rdcstr GetProcessDesc(LPCSTR lpApplicationName, LPCSTR lpCommandLine)
  {
    if(lpApplicationName)
      return lpApplicationName;
    if(lpCommandLine)
      return lpCommandLine;
    return "<unknown>";
  }

  static BOOL WINAPI CreateProcessA_hook(
      __in_opt LPCSTR lpApplicationName, __inout_opt LPSTR lpCommandLine,
      __in_opt LPSECURITY_ATTRIBUTES lpProcessAttributes,
      __in_opt LPSECURITY_ATTRIBUTES lpThreadAttributes, __in BOOL bInheritHandles,
      __in DWORD dwCreationFlags, __in_opt LPVOID lpEnvironment, __in_opt LPCSTR lpCurrentDirectory,
      __in LPSTARTUPINFOA lpStartupInfo, __out LPPROCESS_INFORMATION lpProcessInformation)
  {
    return Hooked_CreateProcess(
        "CreateProcessA",
        [=](DWORD flags, LPVOID env, LPPROCESS_INFORMATION pi) {
          return syshooks.CreateProcessA()(lpApplicationName, lpCommandLine, lpProcessAttributes,
                                           lpThreadAttributes, bInheritHandles, flags, env,
                                           lpCurrentDirectory, lpStartupInfo, pi);
        },
        dwCreationFlags, ShouldInject(lpApplicationName, lpCommandLine), lpEnvironment,
        lpProcessInformation, GetProcessDesc(lpApplicationName, lpCommandLine));
  }

  static BOOL WINAPI CreateProcessW_hook(__in_opt LPCWSTR lpApplicationName,
                                         __inout_opt LPWSTR lpCommandLine,
                                         __in_opt LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                         __in_opt LPSECURITY_ATTRIBUTES lpThreadAttributes,
                                         __in BOOL bInheritHandles, __in DWORD dwCreationFlags,
                                         __in_opt LPVOID lpEnvironment,
                                         __in_opt LPCWSTR lpCurrentDirectory,
                                         __in LPSTARTUPINFOW lpStartupInfo,
                                         __out LPPROCESS_INFORMATION lpProcessInformation)
  {
    return Hooked_CreateProcess(
        "CreateProcessW",
        [=](DWORD flags, LPVOID env, LPPROCESS_INFORMATION pi) {
          return syshooks.CreateProcessW()(lpApplicationName, lpCommandLine, lpProcessAttributes,
                                           lpThreadAttributes, bInheritHandles, flags, env,
                                           lpCurrentDirectory, lpStartupInfo, pi);
        },
        dwCreationFlags, ShouldInject(lpApplicationName, lpCommandLine), lpEnvironment,
        lpProcessInformation, GetProcessDesc(lpApplicationName, lpCommandLine));
  }

  static BOOL WINAPI API110CreateProcessA_hook(
      __in_opt LPCSTR lpApplicationName, __inout_opt LPSTR lpCommandLine,
      __in_opt LPSECURITY_ATTRIBUTES lpProcessAttributes,
      __in_opt LPSECURITY_ATTRIBUTES lpThreadAttributes, __in BOOL bInheritHandles,
      __in DWORD dwCreationFlags, __in_opt LPVOID lpEnvironment, __in_opt LPCSTR lpCurrentDirectory,
      __in LPSTARTUPINFOA lpStartupInfo, __out LPPROCESS_INFORMATION lpProcessInformation)
  {
    return Hooked_CreateProcess(
        "CreateProcessA",
        [=](DWORD flags, LPVOID env, LPPROCESS_INFORMATION pi) {
          return syshooks.API110CreateProcessA()(
              lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
              bInheritHandles, flags, env, lpCurrentDirectory, lpStartupInfo, pi);
        },
        dwCreationFlags, ShouldInject(lpApplicationName, lpCommandLine), lpEnvironment,
        lpProcessInformation, GetProcessDesc(lpApplicationName, lpCommandLine));
  }

  static BOOL WINAPI API110CreateProcessW_hook(
      __in_opt LPCWSTR lpApplicationName, __inout_opt LPWSTR lpCommandLine,
      __in_opt LPSECURITY_ATTRIBUTES lpProcessAttributes,
      __in_opt LPSECURITY_ATTRIBUTES lpThreadAttributes, __in BOOL bInheritHandles,
      __in DWORD dwCreationFlags, __in_opt LPVOID lpEnvironment, __in_opt LPCWSTR lpCurrentDirectory,
      __in LPSTARTUPINFOW lpStartupInfo, __out LPPROCESS_INFORMATION lpProcessInformation)
  {
    return Hooked_CreateProcess(
        "CreateProcessW",
        [=](DWORD flags, LPVOID env, LPPROCESS_INFORMATION pi) {
          return syshooks.API110CreateProcessW()(
              lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
              bInheritHandles, flags, env, lpCurrentDirectory, lpStartupInfo, pi);
        },
        dwCreationFlags, ShouldInject(lpApplicationName, lpCommandLine), lpEnvironment,
        lpProcessInformation, GetProcessDesc(lpApplicationName, lpCommandLine));
  }

  static BOOL WINAPI API111CreateProcessA_hook(
      __in_opt LPCSTR lpApplicationName, __inout_opt LPSTR lpCommandLine,
      __in_opt LPSECURITY_ATTRIBUTES lpProcessAttributes,
      __in_opt LPSECURITY_ATTRIBUTES lpThreadAttributes, __in BOOL bInheritHandles,
      __in DWORD dwCreationFlags, __in_opt LPVOID lpEnvironment, __in_opt LPCSTR lpCurrentDirectory,
      __in LPSTARTUPINFOA lpStartupInfo, __out LPPROCESS_INFORMATION lpProcessInformation)
  {
    return Hooked_CreateProcess(
        "CreateProcessA",
        [=](DWORD flags, LPVOID env, LPPROCESS_INFORMATION pi) {
          return syshooks.API111CreateProcessA()(
              lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
              bInheritHandles, flags, env, lpCurrentDirectory, lpStartupInfo, pi);
        },
        dwCreationFlags, ShouldInject(lpApplicationName, lpCommandLine), lpEnvironment,
        lpProcessInformation, GetProcessDesc(lpApplicationName, lpCommandLine));
  }

  static BOOL WINAPI API111CreateProcessW_hook(
      __in_opt LPCWSTR lpApplicationName, __inout_opt LPWSTR lpCommandLine,
      __in_opt LPSECURITY_ATTRIBUTES lpProcessAttributes,
      __in_opt LPSECURITY_ATTRIBUTES lpThreadAttributes, __in BOOL bInheritHandles,
      __in DWORD dwCreationFlags, __in_opt LPVOID lpEnvironment, __in_opt LPCWSTR lpCurrentDirectory,
      __in LPSTARTUPINFOW lpStartupInfo, __out LPPROCESS_INFORMATION lpProcessInformation)
  {
    return Hooked_CreateProcess(
        "CreateProcessW",
        [=](DWORD flags, LPVOID env, LPPROCESS_INFORMATION pi) {
          return syshooks.API111CreateProcessW()(
              lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
              bInheritHandles, flags, env, lpCurrentDirectory, lpStartupInfo, pi);
        },
        dwCreationFlags, ShouldInject(lpApplicationName, lpCommandLine), lpEnvironment,
        lpProcessInformation, GetProcessDesc(lpApplicationName, lpCommandLine));
  }

  static BOOL WINAPI API112CreateProcessA_hook(
      __in_opt LPCSTR lpApplicationName, __inout_opt LPSTR lpCommandLine,
      __in_opt LPSECURITY_ATTRIBUTES lpProcessAttributes,
      __in_opt LPSECURITY_ATTRIBUTES lpThreadAttributes, __in BOOL bInheritHandles,
      __in DWORD dwCreationFlags, __in_opt LPVOID lpEnvironment, __in_opt LPCSTR lpCurrentDirectory,
      __in LPSTARTUPINFOA lpStartupInfo, __out LPPROCESS_INFORMATION lpProcessInformation)
  {
    return Hooked_CreateProcess(
        "CreateProcessA",
        [=](DWORD flags, LPVOID env, LPPROCESS_INFORMATION pi) {
          return syshooks.API112CreateProcessA()(
              lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
              bInheritHandles, flags, env, lpCurrentDirectory, lpStartupInfo, pi);
        },
        dwCreationFlags, ShouldInject(lpApplicationName, lpCommandLine), lpEnvironment,
        lpProcessInformation, GetProcessDesc(lpApplicationName, lpCommandLine));
  }

  static BOOL WINAPI API112CreateProcessW_hook(
      __in_opt LPCWSTR lpApplicationName, __inout_opt LPWSTR lpCommandLine,
      __in_opt LPSECURITY_ATTRIBUTES lpProcessAttributes,
      __in_opt LPSECURITY_ATTRIBUTES lpThreadAttributes, __in BOOL bInheritHandles,
      __in DWORD dwCreationFlags, __in_opt LPVOID lpEnvironment, __in_opt LPCWSTR lpCurrentDirectory,
      __in LPSTARTUPINFOW lpStartupInfo, __out LPPROCESS_INFORMATION lpProcessInformation)
  {
    return Hooked_CreateProcess(
        "CreateProcessW",
        [=](DWORD flags, LPVOID env, LPPROCESS_INFORMATION pi) {
          return syshooks.API112CreateProcessW()(
              lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
              bInheritHandles, flags, env, lpCurrentDirectory, lpStartupInfo, pi);
        },
        dwCreationFlags, ShouldInject(lpApplicationName, lpCommandLine), lpEnvironment,
        lpProcessInformation, GetProcessDesc(lpApplicationName, lpCommandLine));
  }

  static BOOL WINAPI CreateProcessAsUserA_hook(
      HANDLE hToken, LPCSTR lpApplicationName, LPSTR lpCommandLine,
      LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
      BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCSTR lpCurrentDirectory,
      LPSTARTUPINFOA lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
  {
    return Hooked_CreateProcess(
        "CreateProcessAsUserA",
        [=](DWORD flags, LPVOID env, LPPROCESS_INFORMATION pi) {
          return syshooks.CreateProcessAsUserA()(
              hToken, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
              bInheritHandles, flags, env, lpCurrentDirectory, lpStartupInfo, pi);
        },
        dwCreationFlags, ShouldInject(lpApplicationName, lpCommandLine), lpEnvironment,
        lpProcessInformation, GetProcessDesc(lpApplicationName, lpCommandLine));
  }

  static BOOL WINAPI CreateProcessAsUserW_hook(
      HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
      LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
      BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
      LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
  {
    return Hooked_CreateProcess(
        "CreateProcessAsUserW",
        [=](DWORD flags, LPVOID env, LPPROCESS_INFORMATION pi) {
          return syshooks.CreateProcessAsUserW()(
              hToken, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
              bInheritHandles, flags, env, lpCurrentDirectory, lpStartupInfo, pi);
        },
        dwCreationFlags, ShouldInject(lpApplicationName, lpCommandLine), lpEnvironment,
        lpProcessInformation, GetProcessDesc(lpApplicationName, lpCommandLine));
  }

  static BOOL WINAPI CreateProcessWithLogonW_hook(LPCWSTR lpUsername, LPCWSTR lpDomain,
                                                  LPCWSTR lpPassword, DWORD dwLogonFlags,
                                                  LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
                                                  DWORD dwCreationFlags, LPVOID lpEnvironment,
                                                  LPCWSTR lpCurrentDirectory,
                                                  LPSTARTUPINFOW lpStartupInfo,
                                                  LPPROCESS_INFORMATION lpProcessInformation)
  {
    return Hooked_CreateProcess(
        "CreateProcessAsUserW",
        [=](DWORD flags, LPVOID env, LPPROCESS_INFORMATION pi) {
          return syshooks.CreateProcessWithLogonW()(lpUsername, lpDomain, lpPassword, dwLogonFlags,
                                                    lpApplicationName, lpCommandLine, flags, env,
                                                    lpCurrentDirectory, lpStartupInfo, pi);
        },
        dwCreationFlags, ShouldInject(lpApplicationName, lpCommandLine), lpEnvironment,
        lpProcessInformation, GetProcessDesc(lpApplicationName, lpCommandLine));
  }

  static BOOL WINAPI API110CreateProcessAsUserW_hook(
      HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
      LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
      BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
      LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
  {
    return Hooked_CreateProcess(
        "CreateProcessAsUserW",
        [=](DWORD flags, LPVOID env, LPPROCESS_INFORMATION pi) {
          return syshooks.API110CreateProcessAsUserW()(
              hToken, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
              bInheritHandles, flags, env, lpCurrentDirectory, lpStartupInfo, pi);
        },
        dwCreationFlags, ShouldInject(lpApplicationName, lpCommandLine), lpEnvironment,
        lpProcessInformation, GetProcessDesc(lpApplicationName, lpCommandLine));
  }

  static BOOL WINAPI API111CreateProcessAsUserW_hook(
      HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
      LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
      BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
      LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
  {
    return Hooked_CreateProcess(
        "CreateProcessAsUserW",
        [=](DWORD flags, LPVOID env, LPPROCESS_INFORMATION pi) {
          return syshooks.API111CreateProcessAsUserW()(
              hToken, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
              bInheritHandles, flags, env, lpCurrentDirectory, lpStartupInfo, pi);
        },
        dwCreationFlags, ShouldInject(lpApplicationName, lpCommandLine), lpEnvironment,
        lpProcessInformation, GetProcessDesc(lpApplicationName, lpCommandLine));
  }

  static BOOL WINAPI API112CreateProcessAsUserW_hook(
      HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
      LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
      BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
      LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
  {
    return Hooked_CreateProcess(
        "CreateProcessAsUserW",
        [=](DWORD flags, LPVOID env, LPPROCESS_INFORMATION pi) {
          return syshooks.API112CreateProcessAsUserW()(
              hToken, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
              bInheritHandles, flags, env, lpCurrentDirectory, lpStartupInfo, pi);
        },
        dwCreationFlags, ShouldInject(lpApplicationName, lpCommandLine), lpEnvironment,
        lpProcessInformation, GetProcessDesc(lpApplicationName, lpCommandLine));
  }
};

SysHook SysHook::syshooks;
