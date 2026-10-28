/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2014-2026 Baldur Karlsson
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

// This project deliberately references only kernel32.dll (ie. not even the CRT)
// so that when inserted into an application it has as small an overhead/impact
// as possible. Ideally it would be present only to be a pass-through hook and
// the first time only to allocate a little, check if this process should be hooked
// and load the renderdoc dll.
//
// The no-CRT restriction causes some awkward bits and pieces but the dll is simple
// enough that it's not a big issue.

#include "renderdocshim.h"
#include <windows.h>

struct CaptureOptions;
typedef void(__cdecl *pINTERNAL_SetCaptureOptions)(const CaptureOptions *opts);
typedef void(__cdecl *pINTERNAL_SetLogFile)(const char *logfile);
typedef void(__cdecl *pINTERNAL_SetDebugLogFile)(const char *logfile);

#if defined(RELEASE)
#define LOGPRINT(txt) \
  do                  \
  {                   \
  } while(0)
#else
// define this to something to get logging
// #define LOGPRINT(txt) OutputDebugStringW(txt)
#define LOGPRINT(txt) \
  do                  \
  {                   \
  } while(0)
#endif

#define ENABLE_WILDCARD_GLOBAL_HOOK_MATCH 1

#if ENABLE_WILDCARD_GLOBAL_HOOK_MATCH
// no-CRT wildcard substring match (case-insensitive).
// Pattern uses '*' which matches any characters including path separators.
// Returns true if 'pat' matches some substring of 'path'.
// If pat contains no '*', falls back to plain substring (same as FindStringOrdinal).
static bool WildcardMatchOrdinal(const wchar_t *path, const wchar_t *pat)
{
  // no CRT: compute lengths manually
  int patLen = 0;
  for(const wchar_t *p = pat; *p; p++)
    patLen++;

  // fast path: no wildcard -> plain substring match (existing behavior)
  bool hasStar = 0;
  for(int i = 0; i < patLen; i++)
  {
    if(pat[i] == L'*')
    {
      hasStar = 1;
      break;
    }
  }

  if(!hasStar)
    return FindStringOrdinal(FIND_FROMSTART, path, -1, pat, -1, TRUE) >= 0;

  // wildcard path: walk pattern segments separated by '*' against path.
  // We try matching starting at every position in path (substring semantics).
  int pathLen = 0;
  for(const wchar_t *p = path; *p; p++)
    pathLen++;

  // case-insensitive char compare using ASCII fold (safe for paths)
  auto cicmp = [](wchar_t a, wchar_t b) -> bool {
    if(a >= L'A' && a <= L'Z')
      a += 32;
    if(b >= L'A' && b <= L'Z')
      b += 32;
    return a == b;
  };

  // try every starting position in path
  for(int start = 0; start <= pathLen; start++)
  {
    int pi = 0;        // pattern index
    int ti = start;    // path index

    while(pi < patLen)
    {
      if(pat[pi] == L'*')
      {
        // skip consecutive stars
        pi++;
        while(pi < patLen && pat[pi] == L'*')
          pi++;
        // if '*' is at end, everything matches
        if(pi >= patLen)
          return true;
        // try matching the next literal segment at every remaining position in path
        bool found = false;
        for(int j = ti; j <= pathLen; j++)
        {
          // try matching pat[pi..] starting at path[j]
          int pp = pi;
          int jj = j;
          while(pp < patLen && pat[pp] != L'*' && jj < pathLen)
          {
            if(!cicmp(pat[pp], path[jj]))
              goto next_j;
            pp++;
            jj++;
          }
          if(pp >= patLen || pat[pp] == L'*')
          {
            // matched whole segment (or hit next star)
            pi = pp;
            ti = jj;
            found = true;
            break;
          }
        next_j:;
        }
        if(!found)
          break;    // this start position failed, try next
      }
      else
      {
        // literal char match
        if(ti >= pathLen || !cicmp(pat[pi], path[ti]))
          break;
        pi++;
        ti++;
      }
    }

    // pattern fully consumed = match
    if(pi >= patLen)
      return true;

    // pattern not consumed but we broke out -> try next start position
  }

  return false;
}
#endif

// The shim has no CRT and no logging of its own, which makes a global hook that silently fails to
// inject impossible to diagnose from outside. These helpers append a few short lines to the debug
// log that the injector supplies, so the target process itself records how far it got. They are
// best effort: a failure to write the log must never change the behaviour of the target.
static void TraceAppendText(char *buffer, int &length, int capacity, const char *text)
{
  while(*text && length < capacity - 1)
    buffer[length++] = *text++;
  buffer[length] = 0;
}

static void TraceAppendWide(char *buffer, int &length, int capacity, const wchar_t *text)
{
  // no CRT: this stays well under one page so the compiler does not emit a stack probe
  char converted[2048];
  int count =
      WideCharToMultiByte(CP_UTF8, 0, text, -1, converted, (int)sizeof(converted), NULL, NULL);
  if(count <= 0)
  {
    buffer[length] = 0;
    return;
  }

  // count includes the terminating null
  for(int i = 0; i < count - 1 && length < capacity - 1; i++)
    buffer[length++] = converted[i];
  buffer[length] = 0;
}

static void TraceAppendNumber(char *buffer, int &length, int capacity, unsigned int value)
{
  char digits[16];
  int count = 0;

  do
  {
    digits[count++] = char('0' + (value % 10));
    value /= 10;
  } while(value > 0 && count < (int)sizeof(digits));

  while(count > 0 && length < capacity - 1)
    buffer[length++] = digits[--count];
  buffer[length] = 0;
}

static void TraceAppendPadded(char *buffer, int &length, int capacity, unsigned int value)
{
  if(value < 10 && length < capacity - 1)
    buffer[length++] = '0';
  TraceAppendNumber(buffer, length, capacity, value);
}

static void TraceAppendPrefix(char *buffer, int &length, int capacity)
{
  SYSTEMTIME now;
  GetLocalTime(&now);

  TraceAppendText(buffer, length, capacity, "[shim] ");
  TraceAppendPadded(buffer, length, capacity, now.wHour);
  TraceAppendText(buffer, length, capacity, ":");
  TraceAppendPadded(buffer, length, capacity, now.wMinute);
  TraceAppendText(buffer, length, capacity, ":");
  TraceAppendPadded(buffer, length, capacity, now.wSecond);
  TraceAppendText(buffer, length, capacity, " ");
}

static void TraceWrite(const char *path, const char *line)
{
  if(path == NULL || path[0] == 0)
    return;

  int length = 0;
  while(line[length])
    length++;

  if(length == 0)
    return;

  // the injector hands the path over as UTF-8, so convert instead of relying on the ANSI codepage
  // (a %TEMP% with non-ANSI characters in it would otherwise fail to open)
  wchar_t widePath[1024];
  if(MultiByteToWideChar(CP_UTF8, 0, path, -1, widePath,
                         (int)(sizeof(widePath) / sizeof(widePath[0]))) <= 0)
    return;

  HANDLE file = CreateFileW(widePath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if(file == INVALID_HANDLE_VALUE)
    return;

  DWORD written = 0;
  WriteFile(file, line, (DWORD)length, &written, NULL);
  CloseHandle(file);
}

// True when the executable is not the one being matched, but looks like it: the file name of the
// pattern (with any wildcard characters removed) appears in the executable path. Non-matches are
// only logged in that case, so an active global hook does not fill the log with every process on
// the system while still recording the near misses that explain a failed injection.
static bool IsNearMatch(const wchar_t *exepath, const wchar_t *pathmatch)
{
  const wchar_t *name = pathmatch;
  for(const wchar_t *p = pathmatch; *p; p++)
  {
    if(*p == L'\\' || *p == L'/')
      name = p + 1;
  }

  wchar_t fragment[256];
  int count = 0;
  for(const wchar_t *p = name; *p && count < (int)(sizeof(fragment) / sizeof(fragment[0])) - 1; p++)
  {
    if(*p != L'*' && *p != L'?')
      fragment[count++] = *p;
  }
  fragment[count] = 0;

  // nothing literal to compare against, so don't filter anything out
  if(count == 0)
    return true;

  return FindStringOrdinal(FIND_FROMSTART, exepath, -1, fragment, -1, TRUE) >= 0;
}

static void TraceProcess(const char *path, const wchar_t *exepath, bool matched)
{
  const int capacity = 1024;
  char line[capacity];
  int length = 0;

  line[0] = 0;

  TraceAppendPrefix(line, length, capacity);
  TraceAppendWide(line, length, capacity, exepath);
  TraceAppendText(line, length, capacity, matched ? " matched\n" : " not matched\n");
  TraceWrite(path, line);
}

static void TraceEnvironment(const char *path, const char *name, const char *value)
{
  const int capacity = 512;
  char line[capacity];
  int length = 0;

  line[0] = 0;

  TraceAppendPrefix(line, length, capacity);
  TraceAppendText(line, length, capacity, "environment ");
  TraceAppendText(line, length, capacity, name);
  TraceAppendText(line, length, capacity, "=");
  TraceAppendText(line, length, capacity, value);
  TraceAppendText(line, length, capacity, "\n");
  TraceWrite(path, line);
}

static void TraceLoadResult(const char *path, const wchar_t *dllpath, HMODULE module, DWORD error)
{
  const int capacity = 512;
  char line[capacity];
  int length = 0;

  line[0] = 0;

  TraceAppendPrefix(line, length, capacity);

  // name the DLL that was actually injected rather than a hardcoded one
  const wchar_t *name = dllpath;
  for(const wchar_t *p = dllpath; *p; p++)
  {
    if(*p == L'\\' || *p == L'/')
      name = p + 1;
  }
  TraceAppendWide(line, length, capacity, name);

  if(module)
  {
    TraceAppendText(line, length, capacity, " loaded\n");
  }
  else
  {
    TraceAppendText(line, length, capacity, " load failed err=");
    TraceAppendNumber(line, length, capacity, error);
    TraceAppendText(line, length, capacity, "\n");
  }

  TraceWrite(path, line);
}

void CheckHook()
{
  ShimData *data = NULL;

  HANDLE datahandle = OpenFileMappingA(FILE_MAP_READ, FALSE, GLOBAL_HOOK_DATA_NAME);

  if(datahandle == NULL)
  {
    LOGPRINT(L"renderdocshim: can't open global data\n");
    return;
  }

  // map the whole object rather than sizeof(ShimData): the mapping has a fixed size that may not
  // match this build's struct exactly, and the header below tells us which layout it has
  data = (ShimData *)MapViewOfFile(datahandle, FILE_MAP_READ, 0, 0, 0);

  if(data == NULL)
  {
    CloseHandle(datahandle);
    LOGPRINT(L"renderdocshim: can't map global data\n");
    return;
  }

  if(data->magic != SHIM_DATA_MAGIC || data->structSize < sizeof(ShimData))
  {
    // written by a different version of the header: refuse instead of reading the fields at the
    // wrong offsets
    LOGPRINT(
        L"renderdocshim: global data layout doesn't match, rebuild the shim and "
        L"senderdodcmd together\n");

    UnmapViewOfFile(data);
    CloseHandle(datahandle);
    return;
  }

  if(data->pathmatchstring[0] == 0 || data->pathmatchstring[1] == 0 ||
     data->pathmatchstring[2] == 0 || data->pathmatchstring[3] == 0)
  {
    LOGPRINT(L"renderdocshim: invalid pathmatchstring: '");
    LOGPRINT(data->pathmatchstring);
    LOGPRINT(L"'\n");

    UnmapViewOfFile(data);
    CloseHandle(datahandle);
    return;
  }

  // no new[], need to use VirtualAlloc
  const int exepathLen = 1024;
  wchar_t *exepath = (wchar_t *)VirtualAlloc(NULL, exepathLen * sizeof(wchar_t),
                                             MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

  if(exepath)
  {
    // no memset :).
    for(int i = 0; i < exepathLen; i++)
      exepath[i] = 0;

    GetModuleFileNameW(NULL, exepath, exepathLen - 1);

#if ENABLE_WILDCARD_GLOBAL_HOOK_MATCH
    // Wildcard match, falling back to a plain substring if the pattern has no '*'.
    int find = WildcardMatchOrdinal(exepath, data->pathmatchstring) ? 0 : -1;
#else
    // no str*cmp functions
    int find = FindStringOrdinal(FIND_FROMSTART, exepath, -1, data->pathmatchstring, -1, TRUE);
#endif

    if(find >= 0 || IsNearMatch(exepath, data->pathmatchstring))
      TraceProcess(data->debuglog, exepath, find >= 0);

    if(find >= 0)
    {
      LOGPRINT(L"renderdocshim: Hooking into '");
      LOGPRINT(exepath);
      LOGPRINT(L"', based on '");
      LOGPRINT(data->pathmatchstring);
      LOGPRINT(L"'\n");

      // renderdoc.dll reads the hook strategy exactly once while it initialises inside the
      // LoadLibrary below, so the strategy requested by the injector has to be visible in the
      // process environment first. This is the only way to select e.g. early export table hooks
      // when the target was not launched by RenderDoc.
      if(data->hookstrategyvar[0] && data->hookstrategy[0])
      {
        SetEnvironmentVariableA(data->hookstrategyvar, data->hookstrategy);
        TraceEnvironment(data->debuglog, data->hookstrategyvar, data->hookstrategy);
      }

      SetLastError(0);
      HMODULE mod = LoadLibraryW(data->rdocpath);
      DWORD loadError = GetLastError();

      TraceLoadResult(data->debuglog, data->rdocpath, mod, loadError);

      if(mod)
      {
        pINTERNAL_SetCaptureOptions setopts =
            (pINTERNAL_SetCaptureOptions)GetProcAddress(mod, "INTERNAL_SetCaptureOptions");
        pINTERNAL_SetLogFile setlogfile =
            (pINTERNAL_SetLogFile)GetProcAddress(mod, "INTERNAL_SetLogFile");
        pINTERNAL_SetDebugLogFile setdebuglog =
            (pINTERNAL_SetDebugLogFile)GetProcAddress(mod, "INTERNAL_SetDebugLogFile");

        if(setopts)
          setopts((const CaptureOptions *)data->opts);

        if(setlogfile && data->capfile[0])
          setlogfile(data->capfile);

        if(setdebuglog && data->debuglog[0])
          setdebuglog(data->debuglog);
      }
    }
    else
    {
      LOGPRINT(L"renderdocshim: NOT Hooking into '");
      LOGPRINT(exepath);
      LOGPRINT(L"', based on '");
      LOGPRINT(data->pathmatchstring);
      LOGPRINT(L"'\n");
    }

    VirtualFree(exepath, 0, MEM_RELEASE);
  }
  else
  {
    LOGPRINT(L"renderdocshim: Failed to allocate exepath\n");
  }

  UnmapViewOfFile(data);
  CloseHandle(datahandle);
}

DWORD WINAPI CheckHookThread(LPVOID param)
{
  CheckHook();

  // this makes sure that we remove the reference to the shim dll and unload from
  // the target process. That minimises the impact of having the dll inserted into
  // every process
  FreeLibraryAndExitThread((HMODULE)param, 0);
  return 0;
}

BOOL APIENTRY dll_entry(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
  if(ul_reason_for_call == DLL_PROCESS_ATTACH)
  {
    DisableThreadLibraryCalls(hModule);

    // create a thread so that we can perform more complex actions (DllMain must be minimal
    // in size, even this is a bit dodgy).
    CreateThread(NULL, 0, CheckHookThread, (LPVOID)hModule, 0, NULL);
  }

  return TRUE;
}
