/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2019-2025 Baldur Karlsson
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

// win32_libentry.cpp : Defines the entry point for the DLL
#include <tchar.h>
#include <windows.h>
#include "common/common.h"
#include "core/core.h"
#include "hooks/hooks.h"
#include "strings/string_utils.h"
#include "win32_debug_ini.inl"

static BOOL add_hooks()
{
  wchar_t curFile[512];
  GetModuleFileNameW(NULL, curFile, 512);

  rdcstr f = get_basename(strlower(StringFormat::Wide2UTF8(curFile)));
  RDCDEBUG("get_basename=%s", f.c_str());

  // bail immediately if we're in a system process. We don't want to hook, log, anything -
  // this instance is being used for a shell extension.
  if(f == "dllhost.exe" || f == "explorer.exe")
  {
#if ENABLED(RDOC_RELEASE)
    OutputDebugStringA(
        "Detecting shell process! Disabling hooking in dllhost.exe or explorer.exe\n");
#endif
    return TRUE;
  }

  // search for an exported symbol with this name, typically renderdoc__replay__marker
  if(LibraryHooks::Detect(STRINGIZE(RDOC_BASE_NAME) "__replay__marker"))
  {
    RDCDEBUG("Not creating hooks - in replay app");

    RenderDoc::Inst().SetReplayApp(true);

    RenderDoc::Inst().Initialise();

    LibraryHooks::ReplayInitialise();

    return true;
  }

  RenderDoc::Inst().Initialise();

  RDCLOG("Loading into %ls", curFile);

  LibraryHooks::RegisterHooks();

  RDCLOG("After RegisterHooks\nadd_hooks finished !");

  return TRUE;
}

void CreateConsole()
{
  // Check if we already have a console window
  HWND consoleWindow = GetConsoleWindow();
  if(consoleWindow != NULL)
    return;

  // No console exists, allocate a new one
  FILE *conDummy;
  if(!AllocConsole())
  {
    printf("Failed to allocate console!\n");
    return;
  }

  // connect our std fd (std::cout, std::cerr, std::cin) to our console
  freopen_s(&conDummy, "CONOUT$", "w", stdout);
  freopen_s(&conDummy, "CONOUT$", "w", stderr);
  freopen_s(&conDummy, "CONIN$", "r", stdin);
  printf("Successfully CreateConsole!\n");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
  BOOL ret = TRUE;

  switch (ul_reason_for_call)
  {
    case DLL_PROCESS_ATTACH:
      rfx::ReadDebugIni();
      // 请和OUTPUT_LOG_TO_PRINTF配合使用
      if(RenderDoc::Inst().GetDebugIniBool(RFX_SECTION, "enableConsole"))
        CreateConsole();
      ret = add_hooks();
      SetLastError(0);
      break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
      break;
  }

  return ret;
}
