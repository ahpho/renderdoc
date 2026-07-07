REM echo off
setlocal enabledelayedexpansion
REM chcp 65001

REM 检查2个输入：编译输出目录、安装目录
if "%~1"=="" (
    echo 错误: 请提供编译输出目录
    echo 用法: %0 ^<编译输出目录^>
    exit /b 1
)
if "%~2"=="" (
    echo 错误: 请提供安装目录
    echo 用法: %0 ^<安装目录^>
    exit /b 2
)

REM 设置输出目录变量
set "OutDir=%~1"

REM 确保输出目录路径以反斜杠结尾
if not "%OutDir:~-1%"=="\" set "OutDir=%OutDir%\"

REM set "InstallDir=E:\Work\debug\renderdoc_a\_install\"
set "InstallDir=%~2"

copy /Y "%OutDir%*.json" "%InstallDir%"
copy /Y "%OutDir%*.dll" "%InstallDir%"
copy /Y "%OutDir%*.exe" "%InstallDir%"
copy /Y "%OutDir%*.pdb" "%InstallDir%"
xcopy /E /H /C /I /Y /R "%OutDir%pymodules" "%InstallDir%pymodules\"
xcopy /E /H /C /I /Y /R "%OutDir%qtplugins" "%InstallDir%qtplugins\"
copy /Y "%OutDir%..\..\build_armeabi-v7a\bin\org.renderdoc.renderdoccmd.arm32.apk" "%InstallDir%plugins\android\"
copy /Y "%OutDir%..\..\build_arm64-v8a\bin\org.renderdoc.renderdoccmd.arm64.apk" "%InstallDir%plugins\android\"
copy /Y "%OutDir%..\..\build_x86\bin\org.renderdoc.renderdoccmd.x86.apk" "%InstallDir%plugins\android\"
copy /Y "%OutDir%..\..\build_x86_64\bin\org.renderdoc.renderdoccmd.x64.apk" "%InstallDir%plugins\android\"
@echo 已复制 4 个 apk 文件。

REM 拷贝我自己的工具
copy /Y "%OutDir%d3d9.dll" "%InstallDir%工具在这里\安卓模拟器\"
copy /Y "%OutDir%d3d9.pdb" "%InstallDir%工具在这里\安卓模拟器\"
copy /Y "%OutDir%d3d11.dll" "%InstallDir%工具在这里\安卓模拟器\"
copy /Y "%OutDir%d3d11.pdb" "%InstallDir%工具在这里\安卓模拟器\"
copy /Y "%OutDir%senderdod.dll" "%InstallDir%工具在这里\安卓模拟器\"
copy /Y "%OutDir%senderdod.pdb" "%InstallDir%工具在这里\安卓模拟器\"
copy /Y "%OutDir%DummyDll.dll" "%InstallDir%工具在这里\Injector（没用）\"
copy /Y "%OutDir%DummyDll.pdb" "%InstallDir%工具在这里\Injector（没用）\"
copy /Y "%OutDir%Injector.exe" "%InstallDir%工具在这里\Injector（没用）\"
copy /Y "%OutDir%Injector.pdb" "%InstallDir%工具在这里\Injector（没用）\"
copy /Y "%OutDir%Installer.exe" "%InstallDir%工具在这里\Injector（没用）\"
copy /Y "%OutDir%Installer.pdb" "%InstallDir%工具在这里\Injector（没用）\"
del /Q "%InstallDir%d3d9.dll"
del /Q "%InstallDir%d3d9.pdb"
del /Q "%InstallDir%d3d11.dll"
del /Q "%InstallDir%d3d11.pdb"
del /Q "%InstallDir%DummyDll.dll"
del /Q "%InstallDir%DummyDll.pdb"
del /Q "%InstallDir%Injector.exe"
del /Q "%InstallDir%Injector.pdb"
del /Q "%InstallDir%Installer.exe"
del /Q "%InstallDir%Installer.pdb"


REM 完成
echo 完成!
