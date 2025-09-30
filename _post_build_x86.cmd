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

copy /Y "%OutDir%d3dcompiler_47.dll" "%InstallDir%x86\"
copy /Y "%OutDir%dbghelp.dll" "%InstallDir%x86\"
copy /Y "%OutDir%senderdod.dll" "%InstallDir%x86\"
copy /Y "%OutDir%renderdoc.json" "%InstallDir%x86\"
copy /Y "%OutDir%senderdodcmd.exe" "%InstallDir%x86\"
copy /Y "%OutDir%senderdodshim32.dll" "%InstallDir%x86\"
copy /Y "%OutDir%symsrv.dll" "%InstallDir%x86\"
copy /Y "%OutDir%symsrv.yes" "%InstallDir%x86\"

REM 完成
echo 完成!
