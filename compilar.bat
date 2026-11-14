@echo off
echo ====================================================
echo   COMPILANDO SINA-VISUAL PRO (APP + CLI EN C)
echo ====================================================

if not exist "output" mkdir output
if not exist "reports" mkdir reports

set CORE_SRC=src\config.c src\neural_network.c src\dataset.c src\constructor_bmp.c src\classifier.c src\casos.c

echo [1/2] Compilando Aplicacion Visual (SINA-VISUAL.exe)...
gcc -O3 -Wall -Wextra %CORE_SRC% src\main_app.c -o output\SINA-VISUAL.exe -lws2_32 -lshell32 -lm
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Fallo la compilacion de SINA-VISUAL.exe
    pause
    exit /b 1
)

echo [2/2] Compilando version Consola CLI (sina_visual_cli.exe)...
gcc -O3 -Wall -Wextra %CORE_SRC% src\interface.c src\main_cli.c -o output\sina_visual_cli.exe -lm
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Fallo la compilacion de sina_visual_cli.exe
    pause
    exit /b 1
)

echo.
echo [EXITO] Compilacion completada sin errores.
echo  - App Visual : output\SINA-VISUAL.exe
echo  - Modo CLI   : output\sina_visual_cli.exe
echo ====================================================
pause
