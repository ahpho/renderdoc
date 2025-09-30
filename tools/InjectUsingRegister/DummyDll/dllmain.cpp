// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include <iostream>
#include "../../Detours/src/detours.h"

// detours requires at least once export.
void __declspec(dllexport) _stub()
{
  // stubbed
}

void CreateConsole()
{
  FILE *conDummy;
  if(!AllocConsole())
  {
    MessageBoxA(NULL, "Failed to allocate console!", "ERROR", NULL);
    return;
  }

  // connect our std fd (std::cout, std::cerr, std::cin) to our console
  freopen_s(&conDummy, "CONOUT$", "w", stdout);
  freopen_s(&conDummy, "CONOUT$", "w", stderr);
  freopen_s(&conDummy, "CONIN$", "r", stdin);
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        CreateConsole();
        DetourRestoreAfterWith();    // restore IAT
        std::cout << "Successfully injected & restored IAT!" << std::endl;
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

