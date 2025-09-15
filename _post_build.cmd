rem echo off
setlocal enabledelayedexpansion
chcp 65001

REM 检查是否提供了输出目录参数
if "%~1"=="" (
    echo 错误: 请提供输出目录参数
    echo 用法: %0 ^<输出目录^>
    exit /b 1
)

REM 设置输出目录变量
set "OutDir=%~1"

REM 确保输出目录路径以反斜杠结尾
if not "%OutDir:~-1%"=="\" set "OutDir=%OutDir%\"

set "InstallDir=E:\Work\debug\renderdoc_a_install\"

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
