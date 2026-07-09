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

$origin = Join-Path $Tmp "origin.p101"
@"
.init B 9
A V
A #
B V
B #
"@ | Set-Content -NoNewline -Encoding ascii $origin
Invoke-P101Failure -InputValues @("") -ArgsList @("--start", "CV", $origin) `
    -Pattern "--start requires routine key" -Name "reject full start origin"

$wheel = Join-Path $Tmp "decimal-wheel.p101"
@"
.wheel 3
A V
  S
  >A
  S
  :
A #
"@ | Set-Content -NoNewline -Encoding ascii $wheel
$wheelOutput = Invoke-P101 -InputValues @("1", "8") -ArgsList @($wheel)
Assert-Matches $wheelOutput "A\s+0,125" "decimal wheel"

$fixedPrint = Join-Path $Tmp "fixed-print.p101"
@"
.wheel 2
.init B 3
A V
B #
  S
  >A
A #
"@ | Set-Content -NoNewline -Encoding ascii $fixedPrint
$fixedOutput = Invoke-P101 -InputValues @("4.5") -ArgsList @($fixedPrint)
Assert-Matches $fixedOutput "B\s+3,00[\s\S]*A\s+4,50" "fixed decimal printing"

$cardA = Join-Path $Tmp "card-a.p101"
@"
.wheel 0
A V
  S
B <M
  S
"@ | Set-Content -NoNewline -Encoding ascii $cardA
$cardB = Join-Path $Tmp "card-b.p101"
@"
.wheel 0
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

Invoke-P101Failure -InputValues @("1 2") -ArgsList @("examples/factorial.p101") `
    -Pattern "invalid input: 1 2" -Name "embedded numeric whitespace"

$sqrt = Join-Path $Tmp "sqrt2.p101"
@"
.wheel 15
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
.init B/ 123456789012
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

$spacedSlash = Join-Path $Tmp "spaced-slash.p101"
@"
A V
B / #
"@ | Set-Content -NoNewline -Encoding ascii $spacedSlash
Invoke-P101Failure -InputValues @("") -ArgsList @($spacedSlash) `
    -Pattern "expected one key chord" -Name "reject spaced slash chord"

$rightHalfOverflow = Join-Path $Tmp "right-half-overflow.p101"
@"
.init B/ 1
A V
  S
B <M
"@ | Set-Content -NoNewline -Encoding ascii $rightHalfOverflow
Invoke-P101Failure -InputValues @("123456789012") -ArgsList @($rightHalfOverflow) `
    -Pattern "register B capacity exceeded" -Name "right half capacity"

$mulR = Join-Path $Tmp "mul-r.p101"
@"
.wheel 0
.init A 99999999999.99999999999
.init B 99999999999.99999999999
A V
B x
R #
"@ | Set-Content -NoNewline -Encoding ascii $mulR
$mulROutput = Invoke-P101 -InputValues @("") -ArgsList @($mulR)
Assert-Matches $mulROutput "R\s+9999999999999999999998,0000000000000000000001" `
    "44-digit multiply result"

$mulOverflow = Join-Path $Tmp "mul-overflow.p101"
@"
.wheel 0
.init A 99999999999999999999999
.init B 9999999999999999999999
A V
B x
R #
"@ | Set-Content -NoNewline -Encoding ascii $mulOverflow
Invoke-P101Failure -InputValues @("") -ArgsList @($mulOverflow) `
    -Pattern "register R capacity exceeded .*45 digits" -Name "multiply capacity"

$overlay = Join-Path $Tmp "overlay-clear.p101"
@"
.wheel 0
.init B 7
.init B/ 3
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
