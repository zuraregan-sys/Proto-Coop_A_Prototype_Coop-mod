$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Tools = Join-Path $Root "local\tools"
$Llvm = Join-Path $Tools "llvm-mingw-20261104-ucrt-x86_64\bin"
$MinHook = Join-Path $Tools "minhook-1.3.4"
$Build = Join-Path $Root "build\coop"
$Src = Join-Path $Root "src\coop"

if (-not (Test-Path $Llvm)) { throw "llvm-mingw not found at $Llvm" }
if (-not (Test-Path $MinHook)) { throw "MinHook not found at $MinHook" }
New-Item -ItemType Directory -Force -Path $Build | Out-Null

$Clang = Join-Path $Llvm "i686-w64-mingw32-clang.exe"
$ClangPP = Join-Path $Llvm "i686-w64-mingw32-clang++.exe"
$Common = @("--target=i686-w64-mingw32", "-O2", "-fno-exceptions", "-fno-rtti", "-D_WIN32_WINNT=0x0601", "-D_WIN32_LEAN_AND_MEAN")
$Includes = @("-I$MinHook\include", "-I$MinHook\src", "-I$MinHook\src\hde")

$MinHookSources = @(
    "$MinHook\src\buffer.c",
    "$MinHook\src\hook.c",
    "$MinHook\src\trampoline.c",
    "$MinHook\src\hde\hde32.c",
    "$MinHook\src\hde\hde64.c"
)
$Objects = @()
foreach ($source in $MinHookSources) {
    $name = [IO.Path]::GetFileNameWithoutExtension($source)
    $obj = Join-Path $Build "$name.o"
    & $Clang @Common @Includes -c $source -o $obj
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $source" }
    $Objects += $obj
}

$DiagObj = Join-Path $Build "diagnostic.o"
& $ClangPP @Common @Includes -c "$Src\diagnostic.cpp" -o $DiagObj
if ($LASTEXITCODE -ne 0) { throw "compile failed: diagnostic.cpp" }

$Out = Join-Path $Build "prototype_coop.asi"
& $ClangPP @Common -shared -o $Out $DiagObj $Objects -static -static-libgcc -static-libstdc++ -lws2_32
if ($LASTEXITCODE -ne 0) { throw "link failed" }
Write-Host "built $Out"

$ProxyOut = Join-Path $Build "xinput1_3.dll"
& $ClangPP @Common -shared -o $ProxyOut "$Src\xinput_proxy.cpp" "$Src\xinput1_3.def" -static -static-libgcc -static-libstdc++
if ($LASTEXITCODE -ne 0) { throw "xinput proxy link failed" }
Write-Host "built $ProxyOut"
