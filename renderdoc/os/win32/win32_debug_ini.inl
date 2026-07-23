/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2025 Kuangsihao, Juscent
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

#pragma once

#include "./win32_debug_ini.h"

namespace rfx
{

void trim(char *str)
{
  char *p = str;
  while(*p && isspace(*p))
    ++p;
  if(p != str)
    memmove(str, p, strlen(p) + 1);
  int len = (int)strlen(str);
  while(len > 0 && isspace(str[len - 1]))
    str[--len] = '\0';
}

void ReadDebugIni()
{
  LOGCAT_D("[rfx-so] ReadRfxDebugIni: \n");

  // 获取DLL的模块句柄
  HMODULE hModule = GetModuleHandleA(STRINGIZE(RDOC_BASE_NAME) ".dll");
  if(hModule == NULL)
  {
    LOGCAT_D("[rfx-so] GetModuleHandleA failed!\n");
    return;    // DLL未加载
  }

  // 获取DLL的完整路径
  char path[MAX_PATH];
  DWORD result = GetModuleFileNameA(hModule, path, MAX_PATH);
  if(result == 0)
  {
    LOGCAT_D("[rfx-so] GetModuleFileNameA failed!\n");
    return;    // 获取module失败
  }

  char *lastSlash = strrchr(path, '\\');
  if(!lastSlash)
    return;
  *(lastSlash + 1) = '\0';    // 去掉文件名，保留目录路径
  strcat_s(path, "debug.ini");

  FILE *fpDebugIni = NULL;
  errno_t err = fopen_s(&fpDebugIni, path, "r");
  if(!(err != 0 && fpDebugIni != NULL))
    return;
  LOGCAT_D("[rfx-so] debug.ini found!\n");

  char line[512] = {0};
  bool in_section = false;
#define VALUE_LEN_ 256
  char section_header[VALUE_LEN_] = {0};
  snprintf(section_header, sizeof(section_header), "[%s]", RFX_SECTION);
  char pair_value[VALUE_LEN_] = {0}, section_and_key[512] = {0};

  while(fgets(line, sizeof(line), fpDebugIni))
  {
    // LOGCAT_D("[rfx-so] line=%s\n", line);
    trim(line);
    if(line[0] == ';' || line[0] == '#' || line[0] == '\0')
      continue;

    if(line[0] == '[')
    {
      in_section = (strcmp(line, section_header) == 0);
      continue;
    }

    if(in_section)
    {
      // seperate
      const char *eq = strchr(line, '=');
      if(!eq)
        continue;
      size_t key_len = eq - line;

      // value
      const char *value = eq + 1;
      trim((char *)value);
      strncpy_s(pair_value, value, VALUE_LEN_ - 1);
      pair_value[VALUE_LEN_ - 1] = '\0';

      // save pair
      rdcstr key(line, key_len);
      snprintf(section_and_key, sizeof(section_and_key), "%s/%s", RFX_SECTION, key.c_str());
      RenderDoc::Inst().SetDebugIniValue(RFX_SECTION, key, pair_value);
      // s_DebugIni.m_iniValues[section_and_key] = pair_value;
      LOGCAT_D("[rfx-so] %s=%s.\n", key.c_str(), pair_value);
    }
  }
  LOGCAT_D("[rfx-so] fpDebugIni closed.\n");
  fclose(fpDebugIni);
#undef VALUE_LEN_
}

}