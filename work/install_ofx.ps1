# Elevated helper: install the freshly built DeepGlow2.ofx over the
# Program Files bundle.  Launched by the agent through `Start-Process -Verb
# RunAs`, which raises a normal Windows UAC consent prompt.
$ErrorActionPreference = 'Stop'

$src    = 'X:\coding\DeepGlow\build\Release\DeepGlow2.ofx'
$dstDir = 'C:\Program Files\Common Files\OFX\Plugins\DeepGlow2.ofx.bundle\Contents\Win64'
$dst    = Join-Path $dstDir 'DeepGlow2.ofx'
$log    = 'C:\Users\Oskar\AppData\Local\Temp\opencode\install_ofx.log'

try {
    if (-not (Test-Path $src))    { throw "build artifact missing: $src" }
    if (-not (Test-Path $dstDir)) { throw "bundle dir missing: $dstDir" }

    $before = (Get-FileHash $dst -Algorithm SHA256).Hash
    Copy-Item $src $dst -Force
    $after  = (Get-FileHash $dst -Algorithm SHA256).Hash
    $item   = Get-Item $dst

    "OK"                                              | Set-Content $log
    "before=$($before.Substring(0,16))"               | Add-Content $log
    "after=$($after.Substring(0,16))"                 | Add-Content $log
    "size=$($item.Length)"                            | Add-Content $log
    "mtime=$($item.LastWriteTime.ToString('s'))"      | Add-Content $log
} catch {
    "FAILED: $($_.Exception.Message)" | Set-Content $log
    exit 1
}
