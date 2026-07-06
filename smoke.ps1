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

function Invoke-P101Failure {
    param(
        [string[]]$InputValues,
        [string[]]$ArgsList,
        [string]$Pattern,
        [string]$Name
    )

    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = $InputValues | & $Exe @ArgsList 2>&1
    } finally {
        $ErrorActionPreference = $oldPreference
    }
    $text = ($output | Out-String)
    if ($LASTEXITCODE -eq 0) {
        throw "$Name failed. Expected p101 to fail for $($ArgsList -join ' ')"
    }
    if ($text -notmatch $Pattern) {
        throw "$Name failed. Expected pattern: $Pattern`nActual:`n$text"
    }
}

$factorial = Invoke-P101 -InputValues @("5") -ArgsList @("examples/factorial.p101")
Assert-Matches $factorial "D\s+120" "factorial"

$prime = Invoke-P101 -InputValues @("120") -ArgsList @("examples/prime_factors.p101")
Assert-Matches $prime "C\s+2[\s\S]*C\s+2[\s\S]*C\s+2[\s\S]*C\s+3[\s\S]*B\s+5" "prime factors"

$sine = Invoke-P101 -InputValues @("30") -ArgsList @("examples/sine_cosine.p101")
Assert-Matches $sine "A\s+0,4999999919" "sine"

$cosine = Invoke-P101 -InputValues @("30") -ArgsList @("--start", "W", "examples/sine_cosine.p101")
Assert-Matches $cosine "A\s+0,8660254042" "cosine"

$cubic = Invoke-P101 -InputValues @("27") -ArgsList @("examples/cubic_root.p101")
Assert-Matches $cubic "C\s+3" "cubic root"

$polygon = Invoke-P101 -InputValues @("0", "0", "0", "1", "1", "1", "1", "0", "W") -ArgsList @("examples/polygon_area.p101")
Assert-Matches $polygon "A\s+1" "polygon area"

$chainExample = Invoke-P101 -InputValues @() -ArgsList @("--input", "examples/chaining.input", "examples/chaining_card1.p101")
Assert-Matches $chainExample "A\s+49" "chaining example"

$Tmp = Join-Path $Root ".smoke_tmp"
if (Test-Path $Tmp) {
    Remove-Item -Recurse -Force $Tmp
}
New-Item -ItemType Directory -Path $Tmp | Out-Null

$manualStop = Invoke-P101 -InputValues @("ENTER 5", "D <M", "D #", "START") -ArgsList @("examples/factorial.p101")
Assert-Matches $manualStop "D\s+5[\s\S]*D\s+120" "manual input at stop"

$calc = Invoke-P101 -InputValues @("ENTER 12", "B <M", "ENTER 3", ">A", "B x") -ArgsList @("--calc")
Assert-Matches $calc "A\s+36" "calculator mode"

$origin = Join-Path $Tmp "origin.p101"
@"
.set B 9
A V
A #
B V
B #
"@ | Set-Content -NoNewline -Encoding ascii $origin
$originOutput = Invoke-P101 -InputValues @("") -ArgsList @("--start", "CV", $origin)
Assert-Matches $originOutput "B\s+9" "full start origin"

$override = Join-Path $Tmp "decimal-override.p101"
@"
.decimals 0
A V
  S
  >A
  S
  :
A #
"@ | Set-Content -NoNewline -Encoding ascii $override
$overrideOutput = Invoke-P101 -InputValues @("1", "8") -ArgsList @("--decimals", "3", $override)
Assert-Matches $overrideOutput "A\s+0,125" "decimal override"

$cardA = Join-Path $Tmp "card-a.p101"
@"
.decimals 0
A V
  S
B <M
  S
"@ | Set-Content -NoNewline -Encoding ascii $cardA
$cardB = Join-Path $Tmp "card-b.p101"
@"
.decimals 0
A V
B #
"@ | Set-Content -NoNewline -Encoding ascii $cardB
$chainOutput = Invoke-P101 -InputValues @("7", "CARD $cardB", "V") -ArgsList @($cardA)
Assert-Matches $chainOutput "B\s+7" "card chaining"

$clearM = Join-Path $Tmp "clear-m.p101"
@"
A V
M *
"@ | Set-Content -NoNewline -Encoding ascii $clearM
Invoke-P101Failure -InputValues @("") -ArgsList @($clearM) `
    -Pattern "register M cannot be cleared" -Name "clear M"

$rsProtected = Join-Path $Tmp "rs-protected.p101"
@"
A V
  S
D <M
R S
R #
"@ | Set-Content -NoNewline -Encoding ascii $rsProtected
Invoke-P101Failure -InputValues @("5") -ArgsList @($rsProtected) `
    -Pattern "register R holds an RS-saved D register pair" -Name "R S protected R"

Invoke-P101Failure -InputValues @("not-a-number") -ArgsList @("examples/factorial.p101") `
    -Pattern "invalid input: not-a-number" -Name "invalid input"

$sqrt = Join-Path $Tmp "sqrt2.p101"
@"
.decimals 15
A V
  S
B <M
B sqrt
A #
"@ | Set-Content -NoNewline -Encoding ascii $sqrt
$sqrtOutput = Invoke-P101 -InputValues @("2") -ArgsList @($sqrt)
Assert-Matches $sqrtOutput "A\s+1,414213562373095" "integer sqrt"

$splitOverflow = Join-Path $Tmp "split-overflow.p101"
@"
.set B/ 123456789012
A V
  S
"@ | Set-Content -NoNewline -Encoding ascii $splitOverflow
Invoke-P101Failure -InputValues @("") -ArgsList @($splitOverflow) `
    -Pattern "register B/ capacity exceeded" -Name "split capacity"

$splitWhole = Join-Path $Tmp "split-whole-overflow.p101"
@"
A V
  S
B <M
B/ #
"@ | Set-Content -NoNewline -Encoding ascii $splitWhole
Invoke-P101Failure -InputValues @("123456789012") -ArgsList @($splitWhole) `
    -Pattern "cannot split register B" -Name "split whole-register overflow"

$rightHalfOverflow = Join-Path $Tmp "right-half-overflow.p101"
@"
.set B/ 1
A V
  S
B <M
"@ | Set-Content -NoNewline -Encoding ascii $rightHalfOverflow
Invoke-P101Failure -InputValues @("123456789012") -ArgsList @($rightHalfOverflow) `
    -Pattern "register B capacity exceeded" -Name "right half capacity"

$overlay = Join-Path $Tmp "overlay-clear.p101"
@"
.decimals 0
.set B 7
.set B/ 3
A V
B #
B/ #
B/ *
  S
B <M
B #
"@ | Set-Content -NoNewline -Encoding ascii $overlay
$overlayOutput = Invoke-P101 -InputValues @("123456789012") -ArgsList @($overlay)
Assert-Matches $overlayOutput "B\s+7[\s\S]*B/\s+3[\s\S]*B\s+123456789012" "register overlay clear"

$tooLong = Join-Path $Tmp "too-long.p101"
1..121 | ForEach-Object { "A V" } | Set-Content -Encoding ascii $tooLong
Invoke-P101Failure -InputValues @("") -ArgsList @($tooLong) `
    -Pattern "too many instructions for P101 memory" -Name "instruction capacity"

$occupied = Join-Path $Tmp "occupied-f.p101"
$lines = @("A V")
1..47 | ForEach-Object { $lines += "/ #" }
$lines += "F #"
$lines | Set-Content -Encoding ascii $occupied
Invoke-P101Failure -InputValues @("") -ArgsList @($occupied) `
    -Pattern "register F is occupied by program instructions" -Name "instruction data sharing"

$shared = Join-Path $Tmp "shared-f.p101"
$lines = @("A V", "S", "F <M", "W")
1..44 | ForEach-Object { $lines += "/ #" }
1..5 | ForEach-Object { $lines += "S" }
$lines += "A W"
$lines += "F #"
$lines | Set-Content -Encoding ascii $shared
$sharedOutput = Invoke-P101 -InputValues @("12345") -ArgsList @($shared)
Assert-Matches $sharedOutput "F\s+12345" "shared instruction data storage"
Invoke-P101Failure -InputValues @("123456") -ArgsList @($shared) `
    -Pattern "register F capacity exceeded .*limit 5" -Name "shared storage capacity"

Remove-Item -Recurse -Force $Tmp

Write-Host "Smoke tests passed."
