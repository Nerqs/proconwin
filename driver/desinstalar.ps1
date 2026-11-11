# Remove o driver do controle virtual (ProconWinXusb) e o certificado local.
#
# Rode na pasta do pacote, num PowerShell como Administrador:
#   powershell -ExecutionPolicy Bypass -File .\desinstalar.ps1

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$installer = Join-Path $here 'procon_install.exe'
$subject = 'CN=ProconWin (certificado local deste PC)'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).
  IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
  Write-Host 'Abra o PowerShell como Administrador e rode de novo.' -ForegroundColor Red
  exit 1
}

Write-Host '1/3 Removendo o dispositivo...'
& $installer uninstall

Write-Host '2/3 Removendo o pacote do driver...'
# Acha o oemNN.inf que o Windows criou para o ProconWinXusb.inf.
$current = $null
foreach ($line in (pnputil /enum-drivers)) {
  if ($line -match '^\s*[^:]+:\s+(oem\d+\.inf)\s*$') { $current = $Matches[1] }
  elseif ($current -and $line -match 'proconwinxusb\.inf') {
    pnputil /delete-driver $current /uninstall /force | Out-Host
    $current = $null
  }
}

Write-Host '3/3 Removendo o certificado local...'
foreach ($store in @('Root', 'TrustedPublisher', 'My')) {
  Get-ChildItem "Cert:\LocalMachine\$store" | Where-Object Subject -eq $subject | Remove-Item
}

Write-Host 'Pronto.' -ForegroundColor Green
