// Installer.cpp : This file contains the 'main' function. Program execution begins and ends there.

#include <Windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <fstream>
#include <iostream>

//#define TARGET_PAYLOAD_DLL  "payload.dll"
//#define TARGET_INJECTOR     "preinject.exe"
#define TARGET_EXE          "nshm.exe"
#define REGISTRY_ENTRY      "Software\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\" TARGET_EXE

#define _ERROR(str)                                                                \
  do                                                                               \
  {                                                                                \
    std::cout << "[FATAL]: " << str << " winerr: " << GetLastError() << std::endl; \
    int _unused = getchar();                                                       \
    exit(1);                                                                       \
  } while(0);

HKEY openReg(HKEY key, LPCSTR subKey)
{
  HKEY hKey;
  if (RegCreateKeyExA(key, subKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey,
                     NULL) != ERROR_SUCCESS)
    _ERROR("Failed to open registry key!");
  return hKey;
}

void writeReg(HKEY key, LPCSTR val, LPSTR data, DWORD sz)
{
  if (RegSetValueExA(key, val, 0, REG_SZ, (LPBYTE)data, sz) != ERROR_SUCCESS)
    _ERROR("Failed to write registry!");
}

// thanks stackoverflow lol
HANDLE getProcessByName(const TCHAR *name)
{
  PROCESSENTRY32 entry;
  entry.dwSize = sizeof(PROCESSENTRY32);
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

  if (Process32First(snapshot, &entry) == TRUE)
  {
    while (Process32Next(snapshot, &entry) == TRUE)
    {
      if (lstrcmp(entry.szExeFile, name) == 0)
      {
        HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, entry.th32ProcessID);
        CloseHandle(snapshot);
        return hProcess;
      }
    }
  }

  CloseHandle(snapshot);
  return INVALID_HANDLE_VALUE;
}

std::string getExecutablePath()
{
  CHAR path[MAX_PATH];
  HANDLE hProcess = getProcessByName(TEXT(TARGET_EXE));

  if (hProcess == INVALID_HANDLE_VALUE)
    _ERROR("Failed to grab handle to process, is " TARGET_EXE " running?");

  if (GetModuleFileNameExA(hProcess, NULL, path, MAX_PATH) == 0)
    _ERROR("Failed to grab path to process!");

  // close handle
  CloseHandle(hProcess);

  // get directory (ugly but idc it's an example)
  std::string str(path);
  size_t indx = str.find(TARGET_EXE, 0);
  return str.replace(indx, indx + strlen(TARGET_EXE), "");
}

std::string getCurExecutablePath()
{
  CHAR exeFullPath[MAX_PATH];    // Full path
  GetModuleFileNameA(NULL, exeFullPath, MAX_PATH);
  std::string strFullPath = (std::string)(exeFullPath);
  int nStart = strFullPath.find_last_of(TEXT("\\"));
  std::string strExeDir = strFullPath.substr(0, nStart + 1);
  return strExeDir;
}

int main(int argc, const char *argv[])
{
  //std::ifstream payloadDLL(TARGET_PAYLOAD_DLL), injector(TARGET_INJECTOR);
  std::string newPayloadPath, newInjectorPath;
  bool install = true;

  //newPayloadPath = getExecutablePath();
  //newPayloadPath += "payload.dll";
  newInjectorPath = getCurExecutablePath();//getExecutablePath
  newInjectorPath += "Injector.exe";

  // check for uninstall flag
  if (argc > 1 && strcmp(argv[1], "-u") == 0)
    install = false;

  if (install)
  {
    //if (!payloadDLL.is_open() || !injector.is_open())
    //  _ERROR("Failed to find payload && injector!");

    // copy files
    //std::cout << "Copying " TARGET_PAYLOAD_DLL " to " << newPayloadPath << std::endl;
    //if (!CopyFileA(TARGET_PAYLOAD_DLL, newPayloadPath.c_str(), false))
    //  _ERROR("Failed to copy " TARGET_PAYLOAD_DLL "!");
    //std::cout << "Copying " TARGET_INJECTOR " to " << newInjectorPath << std::endl;
    //if (!CopyFileA(TARGET_INJECTOR, newInjectorPath.c_str(), false))
    //  _ERROR("Failed to copy " TARGET_INJECTOR "!");

    // set registry
    std::cout << "Setting HKEY_LOCAL_MACHINE\\" REGISTRY_ENTRY << "..." << std::endl;
    HKEY reg = openReg(HKEY_LOCAL_MACHINE, REGISTRY_ENTRY);
    writeReg(reg, "debugger", (LPSTR)newInjectorPath.c_str(), (DWORD)newInjectorPath.length());
    RegCloseKey(reg);
    std::cout << "Successfully installed Injector && payload! Please restart " TARGET_EXE "!"
              << std::endl;
  }
  else
  {
    // end process
    //std::cout << "Killing " TARGET_EXE "..." << std::endl;
    //HANDLE hProcess = getProcessByName(TEXT(TARGET_EXE));
    //TerminateProcess(hProcess, 1);
    //CloseHandle(hProcess);
    //Sleep(1000);

    // delete reg key
    std::cout << "Deleting " REGISTRY_ENTRY << "..." << std::endl;
    if (RegDeleteKeyA(HKEY_LOCAL_MACHINE, REGISTRY_ENTRY) != ERROR_SUCCESS)
      _ERROR("Failed to delete registry key! Is it installed? Are we elevated?");
    std::cout << "Deleting HKEY_LOCAL_MACHINE\\" << newPayloadPath << "..." << std::endl;

    // remove payload && injector
    //if (!DeleteFileA(newPayloadPath.c_str()))
    //  _ERROR("Failed to delete payload.dll!");
    //std::cout << "Deleting " << newInjectorPath << "..." << std::endl;
    //if (!DeleteFileA(newInjectorPath.c_str()))
    //  _ERROR("Failed to delete Injector.exe!");

    std::cout << "Successfully uninstalled injector && payload from " TARGET_EXE "!" << std::endl;
  }

  return 0;
}
