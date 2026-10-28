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

#include <stdint.h>

// The shared memory is a fixed size, larger than the struct, so that adding fields later does not
// change the mapping size. Both sides map the whole object and use the header below to check that
// the layout matches, instead of failing to map at all.
#define SHIM_DATA_SIZE 16384

// Identifies the layout below. A shim and a senderdodcmd built from different versions of this
// header must refuse to use each other's data rather than read fields at the wrong offsets.
#define SHIM_DATA_MAGIC 0x5344484Du

struct ShimData
{
  uint32_t magic;
  uint32_t structSize;

  wchar_t pathmatchstring[2048];
  wchar_t rdocpath[2048];
  char debuglog[2048];
  char capfile[2048];

  unsigned char opts[512];

  // Optional hook strategy that the shim installs into the process environment before
  // renderdoc.dll is loaded. RenderDoc normally passes this in the environment of a process it
  // launches itself, but a global hook injects into processes started by someone else. Both strings
  // are empty to keep the default strategy.
  char hookstrategyvar[64];
  char hookstrategy[64];
};

static_assert(sizeof(ShimData) <= SHIM_DATA_SIZE, "ShimData does not fit in the shared memory");

#ifdef WIN64
#define GLOBAL_HOOK_DATA_NAME "SenderDodGlobalHookData64"
#define SHIM_DLL_NAME "senderdodshim64.dll"
#else
#define GLOBAL_HOOK_DATA_NAME "SenderDodGlobalHookData32"
#define SHIM_DLL_NAME "senderdodshim32.dll"
#endif
