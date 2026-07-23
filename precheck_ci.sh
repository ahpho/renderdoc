#!/bin/bash
# renderdoc fork 本地 CI 预检 —— push 前跑一遍，避免 CI 翻车
# 用法: bash precheck.sh   (在仓库根目录)
# 退出码 0 = 全过，可 push；非 0 = 有问题，先修

set -e
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

CF="$ROOT/.git/cf15/clang_format/data/bin/clang-format.exe"
RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
pass=0; fail=0
check() { if [ "$1" = "0" ]; then echo -e "${GREEN}✓ $2${NC}"; else echo -e "${RED}✗ $2${NC}"; fi; }

echo "=== 1. commit-msg: 最近 100 个 summary ≤72 字节 ==="
git log --oneline > /tmp/_a
grep -v 7a064ae /tmp/_a > /tmp/_b
head -n 100 /tmp/_b > /tmp/_c
cp /tmp/_c /tmp/_commits
cut -d ' ' -f2- /tmp/_commits > /tmp/_commit_msgs
cnt=$(LC_ALL=C grep -c '.\{73\}' /tmp/_commit_msgs || true)
if [ "$cnt" = "0" ]; then echo -e "${GREEN}✓ 0 个超标${NC}"; else
  echo -e "${RED}✗ $cnt 个超标:${NC}"; LC_ALL=C grep -n '.\{73\}' /tmp/_commit_msgs; fail=1; fi

echo
echo "=== 2. clang-format 15.0.7 幂等 (改完跑一次, 应无 diff) ==="
if [ ! -f "$CF" ]; then echo -e "${RED}✗ clang-format 未装在 .git/cf15${NC}"; fail=1
else
  CLANG_FORMAT="$CF" bash ./util/clang_format_all.sh > /tmp/_cf.log 2>&1
  if git diff --quiet; then echo -e "${GREEN}✓ 幂等 clean${NC}"; else
    echo -e "${RED}✗ 格式化产生了 diff, 请 git add 后重新提交${NC}"; git diff --stat; fail=1; fi
fi

echo
echo "=== 3. 快速 sanity: 关键文件改动自检提示 ==="
echo "  (若你改了以下区域, 务必对照检查脱敏前缀传播)"
echo "   - C++ 函数名加/改 SENDERDOD_ → 检查 renderdoc.i strip / ci.yml exe 名 / verify-docstrings 正则"
echo "   - 改 .vcxproj 的 TargetName / PlatformToolset / SDK → 检查是否所有配置一致、是否需 WindowsSDKTarget.props"
echo "   - 改 CMake RDOC_BASE_NAME → 检查 renderdoc/*.version 文件名、所有 vcxproj TargetName"
echo "   - 新增子模块工程 → 检查 ci.yml checkout submodules、Detours 类预编译 lib"
echo "   - 新增中文源码 → 确保文件是 UTF-8 with BOM, 所在 vcxproj 有 /utf-8"

echo
if [ "$fail" = "0" ]; then echo -e "${GREEN}=== 预检通过，可以 push (记得: 别先 git pull) ===${NC}"
else echo -e "${RED}=== 预检未过，先修再 push ===${NC}"; exit 1; fi
