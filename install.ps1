# Instalador de uma linha do DS2TexInject para Windows: baixa o DS2TexInject-Setup.exe da ultima
# release e executa. Uso (PowerShell):
#   irm https://raw.githubusercontent.com/sidnei-almeida/dead-space-2-texmod-linux/main/install.ps1 | iex
# O instalador acha a pasta do jogo sozinho; se nao achar, abre uma janela para escolher o deadspace2.exe.

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"   # a barra de progresso deixa o Invoke-WebRequest muito lento
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$url = "https://github.com/sidnei-almeida/dead-space-2-texmod-linux/releases/latest/download/DS2TexInject-Setup.exe"
$exe = Join-Path $env:TEMP "DS2TexInject-Setup.exe"

Write-Host "Baixando / Downloading DS2TexInject-Setup.exe..."
Invoke-WebRequest -Uri $url -OutFile $exe -UseBasicParsing

$p = Start-Process -FilePath $exe -ArgumentList "--no-pause" -NoNewWindow -Wait -PassThru
Remove-Item $exe -ErrorAction SilentlyContinue
if ($p.ExitCode -ne 0) { Write-Host "O instalador terminou com erro / Installer failed ($($p.ExitCode))" -ForegroundColor Red }
