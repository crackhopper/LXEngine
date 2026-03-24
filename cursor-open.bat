@echo off
REM 调用 VS 2022 Native Tools
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64

REM 切换到项目目录
cd /d "%~dp0"

REM 打开 cursor
cursor .

pause