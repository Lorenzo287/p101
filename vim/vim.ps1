$Root = $PSScriptRoot
$Nvim = Join-Path $HOME ".local\share\chezmoi\AppData\Local\nvim"

$File = Join-Path $Root ".\ftdetect"
Copy-Item -Path $File -Destination $Nvim -Force -Recurse
$Syntax = Join-Path $Root ".\syntax"
Copy-Item -Path $Syntax -Destination $Nvim -Force -Recurse

chezmoi -v apply
