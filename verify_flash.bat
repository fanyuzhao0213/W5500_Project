@echo off
echo ==========================================
echo J-Link Flash 验证工具
echo ==========================================
echo.
echo 此脚本将验证 Bootloader 和 APP 是否正确烧录
echo.
echo 请确保：
echo 1. J-Link 已连接到开发板
echo 2. 开发板已上电
echo 3. J-Link 软件已安装
echo.
pause

echo.
echo 正在验证 Flash 内容...
echo.

JLink.exe -CommandFile verify_flash.jlink

echo.
echo ==========================================
echo 验证完成
echo ==========================================
pause
