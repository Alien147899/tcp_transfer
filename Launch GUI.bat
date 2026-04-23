@echo off
setlocal
cd /d "%~dp0"
dotnet run --project ".\desktop-gui\LanTransfer.Gui.csproj"
