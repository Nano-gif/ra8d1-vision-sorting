param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,
    [string]$ConfigDir = (Join-Path $ProjectRoot 'Debug')
)

$ErrorActionPreference = 'Stop'

$toolchainBin = 'E:\Softwore\RT_Thread_Studio\RT-ThreadStudio\repo\Extract\ToolChain_Support_Packages\ARM\GNU_Tools_for_ARM_Embedded_Processors\10.2.1\bin'
$gxx = Join-Path $toolchainBin 'arm-none-eabi-g++.exe'
$objcopy = Join-Path $toolchainBin 'arm-none-eabi-objcopy.exe'
$size = Join-Path $toolchainBin 'arm-none-eabi-size.exe'

$eiLib = Join-Path $ConfigDir 'Components\edge_impulse_v15\libei_v15_sdk.a'
if (-not (Test-Path $eiLib)) {
    & (Join-Path $PSScriptRoot 'build_ei_v15_sdk.ps1') -ProjectRoot $ProjectRoot -ConfigDir $ConfigDir
}

$objects = Get-ChildItem $ConfigDir -Recurse -Filter '*.o' |
    Where-Object { $_.FullName -notlike (Join-Path $ConfigDir 'ei_v15_sdk\*') } |
    Sort-Object FullName

if ($objects.Count -eq 0) {
    throw "No project objects found under $ConfigDir"
}

$elf = Join-Path $ConfigDir 'rtthread.elf'
$hex = Join-Path $ConfigDir 'rtthread.hex'
$siz = Join-Path $ConfigDir 'rtthread.siz'
$map = Join-Path $ConfigDir 'rtthread.map'
$rsp = Join-Path $ConfigDir 'link_rtthread.rsp'

function To-GccPath {
    param([string]$Path)
    $full = (Resolve-Path $Path).Path
    $rel = $full.Substring($ProjectRoot.Length).TrimStart('\')
    return './' + ($rel -replace '\\', '/')
}

$args = @(
    '-T', 'script/fsp.ld',
    '-Xlinker', '--gc-sections',
    '-L.',
    "-Wl,-Map,$((To-GccPath $map))",
    '-Xlinker', '--cref',
    '--specs=nano.specs',
    '-march=armv8.1-m.main+mve.fp+fp.dp',
    '-mthumb',
    '-mfpu=fpv5-sp-d16',
    '-mfloat-abi=hard',
    '-ffunction-sections',
    '-fdata-sections',
    '-Wl,--gc-sections,-cref,-u,Reset_Handler',
    '-L', 'script/',
    '-o', (To-GccPath $elf)
)

$args += $objects | ForEach-Object { To-GccPath $_.FullName }
$args += To-GccPath $eiLib
$args += @('-lc', '-lm')

Set-Content -Path $rsp -Value $args -Encoding ASCII

Push-Location $ProjectRoot
try {
    & $gxx "@$((To-GccPath $rsp).Substring(2))"
    if ($LASTEXITCODE -ne 0) {
        throw "Link failed"
    }

    & $objcopy -O ihex (To-GccPath $elf) (To-GccPath $hex)
    if ($LASTEXITCODE -ne 0) {
        throw "objcopy failed"
    }

    & $size --format=berkeley (To-GccPath $elf) | Tee-Object -FilePath $siz
    if ($LASTEXITCODE -ne 0) {
        throw "size failed"
    }
}
finally {
    Pop-Location
}

Write-Host "Linked $elf"
