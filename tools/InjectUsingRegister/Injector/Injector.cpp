// Injector.cpp : Defines the entry point for the application.
//

#include "pch.h"
#include "framework.h"
#include "Injector.h"

#include <Windows.h>
#include "../../Detours/src/detours.h"
#include <string>

#define DLL_NAME "DummyDll.dll"

#define _ERROR(str)                        \
  do                                       \
  {                                        \
    MessageBoxA(NULL, str, "ERROR", NULL); \
    exit(1);                               \
  } while(0);

std::string getDir()
{
  CHAR path[MAX_PATH];
  // grab file path
  if (GetModuleFileNameA(GetModuleHandleA(NULL), path, MAX_PATH) == 0)
    _ERROR("Failed to grab file location!");

  // get directory
  std::string str(path);
  size_t indx = str.find("Injector.exe", 0);
  return str.replace(indx, indx + strlen("Injector.exe"), "");
}

int APIENTRY WinMain (_In_      HINSTANCE   hInstance,
                      _In_opt_  HINSTANCE   hPrevInstance,
                      _In_      PSTR        lpCmdLine,
                      _In_      int         nCmdShow)
{
  // zero-initalize these
  PROCESS_INFORMATION pi{};
  STARTUPINFOA si{};
  DEBUG_EVENT dbg_evt{};
  std::string payloadPath;
  char currDir[MAX_PATH]{};
  MessageBoxA(NULL, lpCmdLine, "currDir", NULL);

  // grab payload path
  payloadPath = getDir();
  payloadPath += DLL_NAME;

  // grab current working directory
  GetCurrentDirectoryA(MAX_PATH, currDir);

  // start original process with our payload dll in the IAT
  LPCSTR detour_path[1] = {payloadPath.c_str()};
  if (!DetourCreateProcessWithDllsA(NULL,
                                   lpCmdLine,
                                   NULL, NULL,
                                   FALSE, DEBUG_ONLY_THIS_PROCESS,
                                   NULL, currDir, &si, &pi, 1,
                                   detour_path, NULL))
    _ERROR("Failed to launch patched executable!");

  // we have to handle debug events.
  // pass debug events until the EXIT_PROCESS_DEBUG_EVENT is passed
  while(WaitForDebugEvent(&dbg_evt, INFINITE) && dbg_evt.dwDebugEventCode != EXIT_PROCESS_DEBUG_EVENT)
    ContinueDebugEvent(dbg_evt.dwProcessId, dbg_evt.dwThreadId, DBG_EXCEPTION_HANDLED);

  return 0;
}
