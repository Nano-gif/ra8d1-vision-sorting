param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,
    [string]$ConfigDir = (Join-Path $ProjectRoot 'Debug')
)

$ErrorActionPreference = 'Stop'

$toolchainBin = 'E:\Softwore\RT_Thread_Studio\RT-ThreadStudio\repo\Extract\ToolChain_Support_Packages\ARM\GNU_Tools_for_ARM_Embedded_Processors\10.2.1\bin'
$gcc = Join-Path $toolchainBin 'arm-none-eabi-gcc.exe'
$gxx = Join-Path $toolchainBin 'arm-none-eabi-g++.exe'
$ar = Join-Path $toolchainBin 'arm-none-eabi-ar.exe'

foreach ($tool in @($gcc, $gxx, $ar)) {
    if (-not (Test-Path $tool)) {
        throw "Tool not found: $tool"
    }
}

$sourceList = Join-Path $ProjectRoot 'Components\edge_impulse_v15\ei_v15_sdk_sources.mk'
$outRoot = Join-Path $ConfigDir 'ei_v15_sdk'
$libPath = Join-Path $ConfigDir 'Components\edge_impulse_v15\libei_v15_sdk.a'

function Read-MakeSources {
    param(
        [string]$Path,
        [string]$Variable
    )

    $lines = Get-Content $Path
    $collect = $false
    $sources = New-Object System.Collections.Generic.List[string]

    foreach ($line in $lines) {
        if ($line -match "^$([regex]::Escape($Variable))\s*:=") {
            $collect = $true
            $line = $line.Substring($line.IndexOf(':=') + 2)
        }
        elseif ($collect -and $line -match '^[A-Z0-9_]+\s*:=') {
            break
        }

        if (-not $collect) {
            continue
        }

        $entry = $line.Trim()
        if ($entry.Length -eq 0 -or $entry.StartsWith('#')) {
            continue
        }

        $entry = $entry.TrimEnd('\').Trim()
        if ($entry.Length -eq 0) {
            continue
        }

        if ($entry.StartsWith('../')) {
            $entry = $entry.Substring(3)
        }

        $sources.Add((Join-Path $ProjectRoot ($entry -replace '/', '\')))
    }

    return $sources
}

$sources = @()
$sources += Read-MakeSources -Path $sourceList -Variable 'EI_SDK_CPP_SRCS'
$sources += Read-MakeSources -Path $sourceList -Variable 'EI_SDK_CC_SRCS'
$sources += Read-MakeSources -Path $sourceList -Variable 'EI_SDK_C_SRCS'
$sources += Join-Path $ProjectRoot 'Components\edge_impulse_v15\ei_v15_inference.cpp'
$sources += Join-Path $ProjectRoot 'Components\edge_impulse_v15\ei_v15_porting.cpp'
$sources = $sources | Where-Object { $_ -notmatch '\\CMSIS\\DSP\\Source\\TransformFunctions\\' -and $_ -notmatch '\\CMSIS\\DSP\\Source\\MatrixFunctions\\arm_mat_ldlt_f32\.c$' }

New-Item -ItemType Directory -Force -Path (Split-Path $libPath) | Out-Null
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null

$commonFlags = @(
    '-mthumb',
    '-mfloat-abi=hard',
    '-mfpu=fpv5-sp-d16',
    '-march=armv8.1-m.main+mve.fp+fp.dp',
    '-O3',
    '-ffunction-sections',
    '-fdata-sections',
    '-flax-vector-conversions',
    '-Wall',
    '-g',
    '-gdwarf-2',
    '-DNDEBUG',
    '-DEI_CLASSIFIER_TFLITE_ENABLE_CMSIS_NN=1',
    '-DEI_CLASSIFIER_ALLOCATION_STATIC=1',
    '-DEI_TENSOR_ARENA_LOCATION=.ai_sram',
    '-DARM_MATH_LOOPUNROLL'
)

$cxxFlags = @(
    '-std=gnu++11',
    '-fabi-version=0',
    '-fno-exceptions',
    '-fno-rtti'
)

$includes = @(
    $ProjectRoot,
    (Join-Path $ProjectRoot 'APP'),
    (Join-Path $ProjectRoot 'Components\edge_impulse_v15'),
    (Join-Path $ProjectRoot 'Components\edge_impulse_v15\edge_impulse_export'),
    (Join-Path $ProjectRoot 'Components\edge_impulse_v15\edge_impulse_export\edge-impulse-sdk'),
    (Join-Path $toolchainBin '..\arm-none-eabi\include\c++\10.2.1\arm-none-eabi\thumb\v8.1-m.main+mve\hard'),
    (Join-Path $ProjectRoot 'rt-thread\include'),
    (Join-Path $ProjectRoot 'rt-thread\libcpu\arm\common'),
    (Join-Path $ProjectRoot 'rt-thread\libcpu\arm\cortex-m85'),
    (Join-Path $ProjectRoot 'rt-thread\components\finsh'),
    (Join-Path $ProjectRoot 'rt-thread\components\drivers\include'),
    (Join-Path $ProjectRoot 'rt-thread\components\libc\compilers\common\include'),
    (Join-Path $ProjectRoot 'rt-thread\components\libc\compilers\newlib'),
    (Join-Path $ProjectRoot 'ra_gen'),
    (Join-Path $ProjectRoot 'ra_cfg\fsp_cfg'),
    (Join-Path $ProjectRoot 'ra_cfg\fsp_cfg\bsp'),
    (Join-Path $ProjectRoot 'ra\fsp\inc\api'),
    (Join-Path $ProjectRoot 'ra\fsp\inc\instances'),
    (Join-Path $ProjectRoot 'ra\fsp\inc'),
    (Join-Path $ProjectRoot 'ra\tes\dave2d\inc'),
    (Join-Path $ProjectRoot 'ra\arm\CMSIS_5\CMSIS\Core\Include')
) | ForEach-Object { '-I' + $_ }

$preinclude = '-include' + (Join-Path $ProjectRoot 'rtconfig_preinc.h')
$objects = New-Object System.Collections.Generic.List[string]

$buildSignature = @(
    'ei_v15_sdk_build_signature_v3',
    ($commonFlags -join "`n"),
    ($cxxFlags -join "`n"),
    ($includes -join "`n"),
    $preinclude
) -join "`n"
$buildSignaturePath = Join-Path $outRoot '.build-signature'

if ((-not (Test-Path $buildSignaturePath)) -or ((Get-Content $buildSignaturePath -Raw) -ne $buildSignature)) {
    Remove-Item -LiteralPath $outRoot -Recurse -Force
    New-Item -ItemType Directory -Force -Path $outRoot | Out-Null
}

foreach ($src in $sources) {
    if (-not (Test-Path $src)) {
        throw "Source not found: $src"
    }

    $relative = $src.Substring($ProjectRoot.Length).TrimStart('\')
    $obj = Join-Path $outRoot (($relative -replace '[:\\/]', '_') + '.o')
    $dep = $obj -replace '\.o$', '.d'
    $objects.Add($obj)

    $needsBuild = -not (Test-Path $obj) -or ((Get-Item $src).LastWriteTime -gt (Get-Item $obj).LastWriteTime)
    if (-not $needsBuild) {
        continue
    }

    New-Item -ItemType Directory -Force -Path (Split-Path $obj) | Out-Null

    if ($src.EndsWith('.c')) {
        $args = @($commonFlags + $includes + @($preinclude, '-std=gnu11', '-MMD', '-MP', '-MF', $dep, '-MT', $obj, '-c', '-o', $obj, $src))
        & $gcc @args
    }
    else {
        $args = @($commonFlags + $includes + @($preinclude) + $cxxFlags + @('-MMD', '-MP', '-MF', $dep, '-MT', $obj, '-c', '-o', $obj, $src))
        & $gxx @args
    }

    if ($LASTEXITCODE -ne 0) {
        throw "Compile failed: $src"
    }
}

if (Test-Path $libPath) {
    Remove-Item -LiteralPath $libPath -Force
}

$batchSize = 40
for ($i = 0; $i -lt $objects.Count; $i += $batchSize) {
    $end = [Math]::Min($i + $batchSize - 1, $objects.Count - 1)
    $batch = $objects[$i..$end]
    & $ar rcs $libPath @batch
    if ($LASTEXITCODE -ne 0) {
        throw "Archive failed: $libPath"
    }
}

Set-Content -LiteralPath $buildSignaturePath -Value $buildSignature -NoNewline

Write-Host "Built $libPath with $($objects.Count) objects"
