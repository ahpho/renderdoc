/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2024-2025 Baldur Karlsson
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

#include <unordered_set>

namespace DXIL
{
typedef rdcarray<uint32_t> BlockArray;
typedef rdcpair<uint32_t, uint32_t> BlockLink;
typedef rdcpair<uint32_t, uint32_t> ConvergentBlockData;
typedef rdcpair<uint32_t, BlockArray> PartialConvergentBlockData;

struct ControlFlow
{
public:
  ControlFlow() = default;
  ControlFlow(const rdcarray<rdcpair<uint32_t, uint32_t>> &links) { Construct(links); }
  void Construct(const rdcarray<rdcpair<uint32_t, uint32_t>> &links);
  rdcarray<uint32_t> GetUniformBlocks() const { return m_UniformBlocks; }
  rdcarray<uint32_t> GetLoopBlocks() const { return m_LoopBlocks; }
  rdcarray<uint32_t> GetDivergentBlocks() const { return m_DivergentBlocks; }
  rdcarray<ConvergentBlockData> GetConvergentBlocks() const { return m_ConvergentBlocks; }
  rdcarray<PartialConvergentBlockData> GetPartialConvergentBlocks() const
  {
    return m_PartialConvergentBlocks;
  }
  uint32_t GetNextUniformBlock(uint32_t from) const;
  bool IsForwardConnection(uint32_t from, uint32_t to) const;

private:
  typedef rdcarray<uint32_t> BlockPath;

  enum class ConnectionState : uint8_t
  {
    Unknown,
    NotConnected,
    Connected,
  };

  enum PathType : uint32_t
  {
    IncLoops = 0,
    NoLoops = 1,
    Count = 2

  };

  enum class DirectPathState
  {
    None,
    Single,
    Multiple
  };

  enum class RouteState
  {
    NoRoute,
    RouteContainsBlock,
    RouteDoesNotContainBlock,
  };

  bool TraceBlockFlow(const size_t pathsType, const uint32_t from, BlockPath &path);
  bool BlockInAllPaths(const size_t pathsType, uint32_t block, uint32_t pathIdx,
                       int32_t startIdx) const;
  int32_t BlockInAnyPath(const size_t pathsType, uint32_t block, uint32_t pathIdx, int32_t startIdx,
                         int32_t steps) const;
  uint32_t BlockInMultipleUniquePaths(const size_t pathsType, uint32_t block, uint32_t pathIdx,
                                      int32_t startIdx, BlockPath &route,
                                      const uint32_t countPaths) const;
  RouteState BlockInAllPossibleRoutes(const size_t pathsType, uint32_t to, uint32_t mustInclude,
                                      uint32_t pathIdx, uint32_t startIdx, BlockArray &route) const;
  bool IsBlockConnected(const size_t pathsType, uint32_t from, uint32_t to) const;
  bool AllPathsContainBlock(uint32_t from, uint32_t to, uint32_t mustInclude) const;
  DirectPathState ComputeDirectPathState(uint32_t from, uint32_t to,
                                         const rdcarray<uint32_t> *pathIdxsToCheck) const;

  uint32_t PATH_END = ~0U;

  std::unordered_set<uint32_t> m_Blocks;
  rdcarray<BlockArray> m_BlockOutLinks;
  rdcarray<BlockArray> m_BlockInLinks;

  mutable rdcarray<bool> m_TracedBlocks;
  mutable rdcarray<bool> m_CheckedPaths;
  mutable rdcarray<BlockPath> m_ValidRoutes;
  const size_t COUNT_PATHS_TYPES = 2;
  rdcarray<rdcarray<uint32_t>> m_BlockPathLinks[PathType::Count];
  rdcarray<BlockPath> m_PathSets[PathType::Count];

  rdcarray<uint32_t> m_UniformBlocks;
  rdcarray<uint32_t> m_LoopBlocks;
  rdcarray<uint32_t> m_DivergentBlocks;
  rdcarray<ConvergentBlockData> m_ConvergentBlocks;
  rdcarray<PartialConvergentBlockData> m_PartialConvergentBlocks;
  mutable rdcarray<rdcarray<ConnectionState>> m_Connections;

  friend rdcstr GenerateGraph(const char *const name, const ControlFlow *graph);
};
};    // namespace DXIL
