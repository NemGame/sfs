@echo off
rem libs: ws2_32
g++ main.cpp -o sfs.exe -lws2_32
if %errorlevel% neq 0 pause
exit /b %errorlevel%