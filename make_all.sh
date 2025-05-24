#!/bin/bash
# 运行方式: ./make_all.sh -d 或 ./make_all.sh -r

# 默认构建类型为Release
BUILD_TYPE="Release"

# 解析命令行参数
while [[ $# -gt 0 ]]; do
  case $1 in
    -d|--debug)
      BUILD_TYPE="Debug"
      shift
      ;;
    -r|--release)
      BUILD_TYPE="Release"
      shift
      ;;
    -h|--help)
      echo "Usage: $0 [OPTIONS]"
      echo "Options:"
      echo "  -d, --debug     Build in Debug mode"
      echo "  -r, --release   Build in Release mode (default)"
      echo "  -h, --help      Show this help message"
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      echo "Use -h or --help for usage information"
      exit 1
      ;;
  esac
done
echo "Building in $BUILD_TYPE mode..."

# 根据构建类型设置编译标志
if [ "$BUILD_TYPE" = "Debug" ]; then
  C_FLAGS="-g"
  CXX_FLAGS="-g"
  LINKER_FLAGS=""
else
  C_FLAGS="-DNDEBUG -ffunction-sections -fdata-sections -fvisibility=hidden"
  CXX_FLAGS="-DNDEBUG -ffunction-sections -fdata-sections -fvisibility=hidden"
  LINKER_FLAGS="-Wl,--gc-sections,--strip-debug"
fi

# 开始构建
# arm64-v8a -----------------------------------------------------------------------
if [ ! -d "build_arm64-v8a" ]; then
  mkdir build_arm64-v8a
fi
pushd build_arm64-v8a
cmake .. -DANDROID_ABI=arm64-v8a -DBUILD_ANDROID=On -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_C_FLAGS="$C_FLAGS" -DCMAKE_CXX_FLAGS="$CXX_FLAGS" -DCMAKE_SHARED_LINKER_FLAGS="$LINKER_FLAGS"
make
# $ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/x86_64-linux-android-strip --strip-program=arm-linux-gnueabihf-strip --strip-debug lib/libVkLayer_GLES_RenderDoc.so
popd

# armeabi-v7a ---------------------------------------------------------------------
if [ ! -d "build_armeabi-v7a" ]; then
  mkdir build_armeabi-v7a
fi
pushd build_armeabi-v7a
cmake .. -DANDROID_ABI=armeabi-v7a -DBUILD_ANDROID=On -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_C_FLAGS="$C_FLAGS" -DCMAKE_CXX_FLAGS="$CXX_FLAGS" -DCMAKE_SHARED_LINKER_FLAGS="$LINKER_FLAGS"
make
popd

# x86_64 --------------------------------------------------------------------------
if [ ! -d "build_x86_64" ]; then
  mkdir build_x86_64
fi
pushd build_x86_64
cmake .. -DANDROID_ABI=x86_64 -DBUILD_ANDROID=On -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_C_FLAGS="$C_FLAGS" -DCMAKE_CXX_FLAGS="$CXX_FLAGS" -DCMAKE_SHARED_LINKER_FLAGS="$LINKER_FLAGS"
make
popd

# x86 -----------------------------------------------------------------------------
if [ ! -d "build_x86" ]; then
  mkdir build_x86
fi
pushd build_x86
cmake .. -DANDROID_ABI=x86 -DBUILD_ANDROID=On -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_C_FLAGS="$C_FLAGS" -DCMAKE_CXX_FLAGS="$CXX_FLAGS" -DCMAKE_SHARED_LINKER_FLAGS="$LINKER_FLAGS"
make
popd

