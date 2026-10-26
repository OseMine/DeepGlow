# Resolves a usable Visual Studio CMake generator for this runner and exposes it
# as the 'generator' step output.
#
# Hardcoding `-G "Visual Studio 17 2022"` breaks as soon as a runner image stops
# shipping VS 2022 (windows-latest moved to a VS 2026-only image, where the
# generator is "Visual Studio 18 2026").
#
# `cmake --help` lists generators even for Visual Studios that are not actually
# installed, so every candidate is probed with a throwaway configure instead of
# trusting the list order alone.
$ErrorActionPreference = 'Stop'

$help = (cmake --help 2>&1) -join "`n"
$candidates = @(
  [regex]::Matches($help, 'Visual Studio \d+ \d{4}') |
    Sort-Object -Property { [int]($_.Value -split ' ')[2] } -Descending |
    ForEach-Object { $_.Value } |
    Select-Object -Unique
)

if (-not $candidates) {
  throw "No Visual Studio CMake generator is listed by cmake on this runner.`n$help"
}

$tempRoot = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [IO.Path]::GetTempPath() }
$probeRoot = Join-Path $tempRoot ("vsgen-probe-" + [guid]::NewGuid().ToString('N'))
$probeSrc = Join-Path $probeRoot 'src'
$probeBuild = Join-Path $probeRoot 'build'
New-Item -ItemType Directory -Path $probeSrc -Force | Out-Null
Set-Content -Path (Join-Path $probeSrc 'CMakeLists.txt') -Value @'
cmake_minimum_required(VERSION 3.20)
project(vsgen_probe LANGUAGES CXX)
'@

$generator = $null
try {
  foreach ($candidate in $candidates) {
    Write-Host "Probing generator: $candidate"
    # -A x64 matches how the real configure is invoked.
    & cmake -S $probeSrc -B $probeBuild -G $candidate -A x64 *> $null
    if ($LASTEXITCODE -eq 0) {
      $generator = $candidate
      break
    }
    Remove-Item -Recurse -Force $probeBuild -ErrorAction SilentlyContinue
  }
}
finally {
  Remove-Item -Recurse -Force $probeRoot -ErrorAction SilentlyContinue
}

if (-not $generator) {
  throw "None of the listed Visual Studio generators works on this runner (tried: $($candidates -join ', '))."
}

Write-Host "Selected CMake generator: $generator"
"generator=$generator" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
