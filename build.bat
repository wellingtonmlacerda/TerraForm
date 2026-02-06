@echo off
setlocal enabledelayedexpansion
set BUILD_DIR=Build

echo Limpando build anterior...
if exist "%BUILD_DIR%" (
  cd "%BUILD_DIR%"
  if exist CMakeCache.txt del /q CMakeCache.txt
  if exist CMakeFiles rmdir /s /q CMakeFiles
  cd ..
  rmdir /s /q "%BUILD_DIR%"
)
mkdir "%BUILD_DIR%"
cd "%BUILD_DIR%"

echo.
echo Gerando projeto Visual Studio 17 2022...
cmake -G "Visual Studio 17 2022" -A x64 ..
if errorlevel 1 (
  echo.
  echo Limpando cache e tentando Visual Studio 16 2019...
  if exist CMakeCache.txt del /q CMakeCache.txt
  if exist CMakeFiles rmdir /s /q CMakeFiles
  cmake -G "Visual Studio 16 2019" -A x64 ..
  if errorlevel 1 (
    echo.
    echo Limpando cache e tentando com Ninja...
    if exist CMakeCache.txt del /q CMakeCache.txt
    if exist CMakeFiles rmdir /s /q CMakeFiles
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
    if errorlevel 1 (
      echo ERRO: Falha ao configurar CMake com todos os geradores
      pause
      exit /b 1
    )
  )
)

echo.
echo Compilando projeto...
cmake --build . --config Release --parallel 4
if errorlevel 1 (
  echo ERRO: Falha na compilacao
  pause
  exit /b 1
)

echo.
echo ===================================
echo Build concluido com sucesso!
echo ===================================
echo.

if exist "Release\\terraformer.exe" (
  echo Executavel encontrado: Release\\terraformer.exe
  echo Iniciando jogo...
  start .\\Release\\terraformer.exe
) else if exist "terraformer.exe" (
  echo Executavel encontrado: terraformer.exe
  echo Iniciando jogo...
  start .\\terraformer.exe
) else (
  echo AVISO: Nao foi possivel encontrar terraformer.exe
  echo Procure em: %CD%\\Release\\ ou %CD%\\Debug\\
  dir /s terraformer.exe
  pause
)
