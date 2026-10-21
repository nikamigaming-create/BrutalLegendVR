$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'scripts\common.ps1')
# Execute the actual selection block without launching a host or changing registry.
$source=Get-Content (Join-Path $root 'scripts\launch_vr.ps1') -Raw
$selection=($source -split [regex]::Escape('$runtimeSource=''command line'''),2)[1]
$block=[scriptblock]::Create('$runtimeSource=''command line'''+"`n"+($selection -split [regex]::Escape('$env:BLVR_CONTROLS_FILE'),2)[0])
$originalRuntime=$env:XR_RUNTIME_JSON
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('blvr-runtime-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
try {
    foreach ($name in 'meta','vdxr','steam','explicit','environment','simulator') {
        Set-Content -LiteralPath (Join-Path $fixture "$name.json") -Value '{}'
    }
    function Get-BlvrActiveRuntime { return $script:registered }
    $cases=@(
        @('','', '', 'vdxr','vdxr'),
        @('','', '', 'steam','steam'),
        @('','', '', 'meta','meta'),
        @('','meta', '', 'vdxr','meta'),
        @('','', 'environment', 'meta','environment'),
        @('','vdxr', 'environment', 'meta','vdxr'),
        @('explicit','vdxr', 'environment', 'meta','explicit')
    )
    foreach ($case in $cases) {
        $RuntimeJson=if ($case[0]) { Join-Path $fixture ($case[0]+'.json') } else { '' }
        $settings=@{runtime_json=$(if ($case[1]) { Join-Path $fixture ($case[1]+'.json') } else { '' })}
        $env:XR_RUNTIME_JSON=if ($case[2]) { Join-Path $fixture ($case[2]+'.json') } else { '' }
        $script:registered=Join-Path $fixture ($case[3]+'.json')
        . $block
        if ($env:XR_RUNTIME_JSON -ne (Join-Path $fixture ($case[4]+'.json'))) { throw "Wrong runtime: $case" }
    }
    foreach ($bad in 'missing','simulator') {
        $RuntimeJson='';$settings=@{};$env:XR_RUNTIME_JSON=''
        $script:registered=Join-Path $fixture ($bad+'.json')
        $failed=$false
        try { . $block } catch { $failed=$true }
        if (-not $failed) { throw "Did not reject $bad runtime" }
    }
    Write-Host 'PASS: 9 runtime selection cases; no host launched or system setting changed.'
} finally {
    $env:XR_RUNTIME_JSON=$originalRuntime
    # Only these known fixture files are removed; no recursive filesystem operation.
    Get-ChildItem -LiteralPath $fixture -File | Remove-Item
    Remove-Item -LiteralPath $fixture
}
