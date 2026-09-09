<#
.SYNOPSIS
    Compile the game's shaders on Windows, producing signed DXIL for D3D12.

.DESCRIPTION
    macOS/Linux (tools/shaders.sh) cannot produce signed DXIL, because signing
    requires Microsoft's dxil.dll. This script fills that gap: it runs dxc.exe
    on the committed assets/shaders/*.hlsl sources to emit assets/shaders/*.dxil.

    If glslc.exe and spirv-cross.exe are also found on PATH, it additionally
    regenerates .spv/.msl/.hlsl from shaders/*.vert|*.frag first (identical
    commands to tools/shaders.sh), so a Windows dev with the full toolchain
    installed can run the whole pipeline from this one script. Otherwise it
    just does the DXIL step from the .hlsl already in the repo.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\shaders.ps1
#>

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Repo root, resolved from this script's own location.
# ---------------------------------------------------------------------------
$Root = Split-Path -Parent $PSScriptRoot
$ShaderSrcDir = Join-Path $Root 'shaders'
$OutDir = Join-Path $Root 'assets\shaders'

if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
}

# ---------------------------------------------------------------------------
# Locate dxc.exe: PATH, then the Windows SDK, then $env:DXC override.
# ---------------------------------------------------------------------------
function Find-Dxc {
    $cmd = Get-Command 'dxc.exe' -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    if ($env:DXC -and (Test-Path $env:DXC)) {
        return $env:DXC
    }

    $sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    if (Test-Path $sdkRoot) {
        $candidates = Get-ChildItem -Path $sdkRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
            Sort-Object { [version]$_.Name } -Descending
        foreach ($dir in $candidates) {
            $candidate = Join-Path $dir.FullName 'x64\dxc.exe'
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }

    return $null
}

$DxcPath = Find-Dxc
if (-not $DxcPath) {
    Write-Error @"
dxc.exe not found.
Install one of:
  - the Windows 10/11 SDK (provides Windows Kits\10\bin\<ver>\x64\dxc.exe)
  - the standalone DirectXShaderCompiler release (https://github.com/microsoft/DirectXShaderCompiler/releases)
Or point `$env:DXC` at a dxc.exe path.
"@
    exit 1
}
Write-Host "dxc: $DxcPath"

# ---------------------------------------------------------------------------
# Decide whether we can also regenerate spv/msl/hlsl from source.
# ---------------------------------------------------------------------------
$GlslcCmd = Get-Command 'glslc.exe' -ErrorAction SilentlyContinue
$SpirvCrossCmd = Get-Command 'spirv-cross.exe' -ErrorAction SilentlyContinue
$FullPipeline = [bool]($GlslcCmd -and $SpirvCrossCmd)

if ($FullPipeline) {
    Write-Host "mode: full pipeline (glslc + spirv-cross + dxc found) — regenerating spv/msl/hlsl/dxil"
} else {
    Write-Host "mode: dxil-only (glslc/spirv-cross not found) — using committed assets/shaders/*.hlsl"
}

# ---------------------------------------------------------------------------
# Collect shader sources.
# ---------------------------------------------------------------------------
if ($FullPipeline) {
    $shaders = Get-ChildItem -Path $ShaderSrcDir -File |
        Where-Object { $_.Extension -eq '.vert' -or $_.Extension -eq '.frag' } |
        Sort-Object Name
    if ($shaders.Count -eq 0) {
        Write-Error "no *.vert / *.frag sources found in $ShaderSrcDir"
        exit 1
    }
} else {
    $shaders = Get-ChildItem -Path $OutDir -File -Filter '*.hlsl' |
        Sort-Object Name
    if ($shaders.Count -eq 0) {
        Write-Error "no *.hlsl found in $OutDir to compile — run tools/shaders.sh first, or install glslc/spirv-cross"
        exit 1
    }
}

# ---------------------------------------------------------------------------
# Per-shader compile.
# ---------------------------------------------------------------------------
$count = 0
foreach ($shader in $shaders) {
    if ($FullPipeline) {
        $name = $shader.Name           # e.g. lit.frag
        $stageExt = $shader.Extension  # .vert or .frag
        $spv = Join-Path $OutDir "$name.spv"
        $msl = Join-Path $OutDir "$name.msl"
        $hlsl = Join-Path $OutDir "$name.hlsl"

        & $GlslcCmd.Source -O $shader.FullName -o $spv
        if ($LASTEXITCODE -ne 0) { throw "glslc failed on $name" }

        & $SpirvCrossCmd.Source --msl --msl-version 20100 --msl-decoration-binding $spv --output $msl
        if ($LASTEXITCODE -ne 0) { throw "spirv-cross (msl) failed on $name" }

        & $SpirvCrossCmd.Source --hlsl --shader-model 60 $spv --output $hlsl
        if ($LASTEXITCODE -ne 0) { throw "spirv-cross (hlsl) failed on $name" }
    } else {
        # $shader.Name is e.g. "lit.frag.hlsl"; strip the trailing .hlsl to
        # recover the shader name ("lit.frag") and its stage extension.
        $name = $shader.BaseName        # lit.frag
        $stageExt = [System.IO.Path]::GetExtension($name)  # .frag
        $hlsl = $shader.FullName
    }

    switch ($stageExt) {
        '.vert' { $profile = 'vs_6_0' }
        '.frag' { $profile = 'ps_6_0' }
        default { throw "cannot determine shader stage for dxc from name: $name" }
    }

    $dxil = Join-Path $OutDir "$name.dxil"
    & $DxcPath -T $profile -E main -Fo $dxil $hlsl
    if ($LASTEXITCODE -ne 0) { throw "dxc failed on $name" }

    if ($FullPipeline) {
        Write-Host "  $name: spv msl hlsl dxil"
    } else {
        Write-Host "  $name: dxil"
    }
    $count++
}

Write-Host "shaders ok ($count shader(s) built)"
