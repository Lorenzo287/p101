$ErrorActionPreference = "Stop"

$Root = $PSScriptRoot
$Exe = Join-Path $Root "p101.exe"
if (-not (Test-Path $Exe)) {
    $Exe = Join-Path $Root "p101"
}

function Invoke-P101 {
    param(
        [string[]]$InputValues,
        [string[]]$ArgsList
    )

    $output = $InputValues | & $Exe @ArgsList
    if ($LASTEXITCODE -ne 0) {
        throw "p101 exited with code $LASTEXITCODE for $($ArgsList -join ' ')"
    }
    return ($output -join "`n")
}

function Assert-Matches {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Name
    )

    if ($Text -notmatch $Pattern) {
        throw "$Name failed. Expected pattern: $Pattern`nActual:`n$Text"
    }
}

$factorial = Invoke-P101 -InputValues @("5") -ArgsList @("examples/factorial.p101")
Assert-Matches $factorial "D\s+120" "factorial"

$prime = Invoke-P101 -InputValues @("120") -ArgsList @("examples/prime_factors.p101")
Assert-Matches $prime "C\s+2[\s\S]*C\s+2[\s\S]*C\s+2[\s\S]*C\s+3[\s\S]*B\s+5" "prime factors"

$sine = Invoke-P101 -InputValues @("30") -ArgsList @("examples/sine_cosine.p101")
Assert-Matches $sine "F\s+0,4999999999" "sine"

$cosine = Invoke-P101 -InputValues @("30") -ArgsList @("--start", "W", "examples/sine_cosine.p101")
Assert-Matches $cosine "F\s+0,8660254039" "cosine"

$cubic = Invoke-P101 -InputValues @("27") -ArgsList @("examples/cubic_root.p101")
Assert-Matches $cubic "C\s+2,9999999998" "cubic root"

Write-Host "Smoke tests passed."
