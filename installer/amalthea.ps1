# SPDX-License-Identifier: GPL-3.0-only
# Amalthea installer for the TRIMUI Brick (TG3040), Windows. Started by install.bat / restore.bat:
#   powershell -NoProfile -ExecutionPolicy Bypass -File amalthea.ps1 install [-Yes]
#   powershell -NoProfile -ExecutionPolicy Bypass -File amalthea.ps1 restore [-Yes] [-Backup <folder>]
# The same steps as install.sh / restore.sh (lib/common.sh), and the same device-side script.
param(
  [Parameter(Position = 0)][ValidateSet('install', 'restore')][string]$Action = 'install',
  [switch]$Yes,
  [string]$Backup = ''
)
$ErrorActionPreference = 'Stop'
$Here = $PSScriptRoot
$DevStage = '/mnt/UDISK/amalthea-install'
$DevBackup = '/mnt/UDISK/amalthea-stock-backup'
$DevSessions = '/mnt/UDISK/tracker-linux'
$TestedKernel = '#804'
$script:Adb = $env:ADB
$script:Serial = ''
$script:Out = ''
$script:P = @{}
$Build = (Get-Content (Join-Path $Here 'payload/BUILD') -Raw).Trim()

function Say([string]$t) { Write-Host $t }
function Step([string]$t) { Write-Host ""; Write-Host "==> $t" }
class StopInstaller : Exception { StopInstaller([string]$m) : base($m) {} }
function Die([string]$t) { throw [StopInstaller]::new($t) }
function Banner([string]$t) { Write-Host ""; Write-Host $t; Write-Host ('=' * $t.Length); Write-Host "" }

function Confirm([string]$q) {
  if ($Yes) { return }
  $a = Read-Host "$q Type yes to continue"
  if ($a -ne 'yes') { Die 'Cancelled. Nothing was changed.' }
}

function Sha256([string]$path) { (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLower() }

function Test-Sums([string]$dir) {
  foreach ($line in Get-Content (Join-Path $dir 'SHA256SUMS')) {
    $parts = $line -split '\s+', 2
    if ($parts.Count -lt 2) { continue }
    if ((Sha256 (Join-Path $dir $parts[1].Trim())) -ne $parts[0]) { Say "checksum mismatch: $($parts[1])"; return $false }
  }
  return $true
}

function Find-Adb {
  if (-not $script:Adb) {
    $cmd = Get-Command adb -ErrorAction SilentlyContinue
    $local = Join-Path $Here 'platform-tools/adb.exe'
    if ($cmd) { $script:Adb = $cmd.Source }
    elseif (Test-Path $local) { $script:Adb = $local }
    else {
      $url = 'https://dl.google.com/android/repository/platform-tools-latest-windows.zip'
      Say 'ADB (Android platform-tools) was not found. It can be downloaded from Google'
      Say "into this folder: $url"
      Confirm 'Download platform-tools now?'
      $zip = Join-Path $Here 'platform-tools.zip'
      Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $zip
      Expand-Archive -Force -Path $zip -DestinationPath $Here
      Remove-Item $zip
      $script:Adb = $local
    }
  }
  & $script:Adb start-server 2>$null | Out-Null
}

function Adb { & $script:Adb -s $script:Serial @args 2>&1 | ForEach-Object { "$_" } }

function Wait-Brick([int]$timeout = 120) {
  $hinted = $false
  while ($true) {
    $lines = @(& $script:Adb devices 2>$null | Select-Object -Skip 1 | Where-Object { $_ -match '\S' })
    $ready = @($lines | Where-Object { $_ -match "`tdevice$" })
    if ($ready.Count -eq 1) { $script:Serial = ($ready[0] -split "`t")[0]; return }
    if ($ready.Count -gt 1) { Die 'More than one Android device is connected. Leave only the Brick connected.' }
    if (-not $hinted) {
      Say 'Waiting for the Brick: switch it on and connect it with a USB-C data cable.'
      Say '(On Amalthea, stay on the home screen: the FX and LIFEBLOOD appliances use the port for the FM-1.)'
      Say '(If Windows does not see it, install the Google USB Driver: https://developer.android.com/studio/run/win-usb)'
      $hinted = $true
    }
    $timeout -= 2
    if ($timeout -le 0) { Die 'No Brick found over ADB.' }
    Start-Sleep -Seconds 2
  }
}

function Push([string]$src, [string]$dst) {
  & $script:Adb -s $script:Serial push $src $dst 2>&1 | Out-Null
  if ($LASTEXITCODE -ne 0) { Die "Could not copy $(Split-Path $src -Leaf) to the Brick." }
}

function Pull([string]$src, [string]$dst) {
  & $script:Adb -s $script:Serial pull $src $dst 2>&1 | Out-Null
  return ($LASTEXITCODE -eq 0)
}

function Push-Installer {
  Step 'Copying the installer to the Brick'
  Adb shell "mkdir -p $DevStage" | Out-Null
  Get-ChildItem (Join-Path $Here 'payload') -File | ForEach-Object { Push $_.FullName "$DevStage/$($_.Name)" }
  Adb shell "chmod 755 $DevStage/busybox" | Out-Null
}

function Device([string]$command) {
  $script:Out = (Adb shell "$DevStage/busybox sh $DevStage/amalthea-device.sh $command") -replace "`r", '' -join "`n"
  foreach ($l in $script:Out -split "`n") { if ($l.StartsWith('LOG ')) { Say ('    ' + $l.Substring(4)) } }
  $result = @($script:Out -split "`n" | Where-Object { $_ -like 'RESULT=*' } | Select-Object -Last 1)
  if ($result.Count -and $result[0] -eq 'RESULT=OK') { return }
  if ($result.Count -and $result[0].StartsWith('RESULT=FAIL')) { Die $result[0].Substring(12) }
  Die "The Brick did not answer as expected:`n$($script:Out)"
}

function Probe {
  Device probe
  $script:P = @{}
  foreach ($l in $script:Out -split "`n") {
    if ($l -match '^([A-Z_]+)=(.*)$' -and -not $script:P.ContainsKey($Matches[1])) { $script:P[$Matches[1]] = $Matches[2] }
  }
  Say "    Brick $($script:Serial): $($script:P.SYSTEM) system, kernel $($script:P.KERNEL), battery $($script:P.BATTERY)%"
}

function Need-Battery {
  if ([int]("0" + $script:P.BATTERY) -lt 50) { Die "The battery is at $($script:P.BATTERY)%. Charge the Brick to at least 50% and run this again." }
}

function Pull-StockBackup {
  $dest = Join-Path $Here "backup-$($script:Serial)"
  if ((Test-Path (Join-Path $dest 'SHA256SUMS')) -and (Test-Sums $dest)) { Say "    A verified copy is already on this computer: $dest"; return }
  New-Item -ItemType Directory -Force -Path $dest | Out-Null
  foreach ($f in 'SHA256SUMS', 'stock-version.txt', 'stock-boot.img', 'stock-rootfs.img') {
    if (-not (Pull "$DevBackup/$f" (Join-Path $dest $f))) { Die "Could not copy $f from the Brick." }
  }
  if (-not (Test-Sums $dest)) { Die "The copied backup does not match the Brick's. Run the installer again." }
  Say "    Saved to $dest (keep this folder: restore.bat uses it)"
}

function Backup-Sessions {
  $dest = Join-Path $Here ("sessions-$($script:Serial)-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
  New-Item -ItemType Directory -Force -Path $dest | Out-Null
  foreach ($f in 'session-slots', 'fx-session-slots', 'lifeblood-session-slots', 'session.amalthea', 'device-settings.amalthea') {
    Pull "$DevSessions/$f" "$dest" | Out-Null
  }
  Say "    Saved sessions copied to $dest"
}

function Wait-Amalthea {
  Step 'Waiting for the Brick to install and start Amalthea (one to three minutes; do not disconnect it)'
  Start-Sleep -Seconds 15
  Wait-Brick 400
  Probe
  if ($script:P.SYSTEM -eq 'amalthea' -and $script:P.MODE -eq 'system') {
    Say ''; Say 'Done. Amalthea is installed and running.'
    Say 'Read the manual: https://nzimas.github.io/amalthea/manual.html'
    return
  }
  if ($script:P.SYSTEM -eq 'recovery') {
    Die "The Brick stopped in Amalthea's RAM recovery: $($script:P.RECOVERY_REASON).`nNothing is lost. Run install.bat again to retry, or restore.bat to return to the stock system."
  }
  Die "The Brick restarted into an unexpected state ($($script:P.SYSTEM), mode $($script:P.MODE))."
}

function Install-FromStock {
  if (-not $script:P.KERNEL.Contains($TestedKernel)) {
    Say ''
    Say "WARNING: this Brick runs stock firmware $($script:P.STOCK_VERSION) (kernel $($script:P.KERNEL))."
    Say "Amalthea was developed on stock 1.0.6 (kernel $TestedKernel). It brings its own kernel,"
    Say 'but on this firmware it is untested.'
    Confirm 'Continue anyway?'
  }
  Need-Battery
  Step 'Backing up the stock system on the Brick'; Device backup
  Step 'Copying the stock backup to this computer'; Pull-StockBackup
  Step 'Unpacking Amalthea on the Brick'; Device stage
  Step 'Writing the Amalthea boot partition'; Device arm
  Step 'Restarting the Brick'; Adb shell reboot | Out-Null
  Wait-Amalthea
}

function Update-Amalthea {
  Say ''
  Say "This Brick already runs Amalthea: it will be updated to build $Build."
  Say 'The instruments restart blank and stopped, as on every start: save your session first.'
  Confirm 'Update now?'
  Step 'Copying your saved sessions to this computer'; Backup-Sessions
  Step 'Unpacking the update on the Brick'; Device stage
  Probe
  $root = ((Get-Content (Join-Path $Here 'payload/rootfs.sha256') -Raw) -split '\s+')[0]
  if ($script:P.INSTALLED_ROOT -eq $root -and $script:P.BOOT_CURRENT -eq 'yes') {
    Step 'Restarting the instruments'
    Adb shell 'kill -TERM $(pidof amalthea)' | Out-Null
    Say ''; Say "Done. Amalthea build $Build is running."
    return
  }
  Need-Battery
  Step 'Writing the Amalthea boot partition'; Device arm
  Step 'Restarting the Brick'; Adb shell reboot | Out-Null
  Wait-Amalthea
}

function Retry-FromRecovery {
  Say ''
  Say "The Brick is in Amalthea's RAM recovery: $($script:P.RECOVERY_REASON)."
  Confirm 'Retry the installation?'
  Device retry-install
  Step 'Restarting the Brick'; Adb shell 'reboot -f' | Out-Null
  Wait-Amalthea
}

function Run-Install {
  Banner "Amalthea installer - build $Build"
  Say @'
Amalthea is not an app on top of the stock firmware. It REPLACES the Brick's
operating system: the boot and root partitions are overwritten by Amalthea Linux,
a minimal custom distribution built around the Amalthea instruments. The
gaming frontend, emulators and every other stock feature will be gone until
you restore the stock system with restore.bat.

Your SD card, the stock settings partition and internal data are not written.
A copy of the stock system is saved on this computer before anything changes.

You do this at your own risk. There is no warranty of any kind (see LICENSE.txt).
'@
  Say ''
  Confirm 'Install Amalthea on the connected Brick?'
  Find-Adb; Wait-Brick; Push-Installer; Probe
  if ($script:P.HW -ne 'TG3040') { Die 'The connected device is not a TRIMUI Brick (TG3040). Nothing was changed.' }
  switch ($script:P.SYSTEM) {
    'stock' { Install-FromStock }
    'amalthea' { Update-Amalthea }
    'recovery' { Retry-FromRecovery }
    default { Die 'The Brick is running a system this installer does not recognise. Nothing was changed.' }
  }
}

function Run-Restore {
  Banner 'Restore the stock TRIMUI system'
  Confirm 'Replace Amalthea with the stock system from your backup?'
  Find-Adb; Wait-Brick; Push-Installer; Probe
  if ($script:P.HW -ne 'TG3040') { Die 'The connected device is not a TRIMUI Brick (TG3040). Nothing was changed.' }
  switch ($script:P.SYSTEM) {
    'stock' { Say 'This Brick is already running the stock system. Nothing to do.'; return }
    'amalthea' {
      if ($script:P.BACKUP -ne 'present') {
        $local = if ($Backup) { $Backup } else { Join-Path $Here "backup-$($script:Serial)" }
        if (-not (Test-Path (Join-Path $local 'SHA256SUMS'))) { Die "No stock backup on the Brick and none at $local. Pass the folder: restore.bat path\to\backup-folder" }
        if (-not (Test-Sums $local)) { Die "The backup in $local is damaged." }
        Step 'Copying the stock backup to the Brick'
        Adb shell "mkdir -p $DevBackup" | Out-Null
        foreach ($f in 'stock-boot.img', 'stock-rootfs.img', 'SHA256SUMS') { Push (Join-Path $local $f) "$DevBackup/$f" }
      }
      Step 'Preparing recovery'; Device restore-prepare
      Step "Restarting into Amalthea's RAM recovery"; Adb shell reboot | Out-Null
      Start-Sleep -Seconds 8
      Wait-Brick 180; Probe
      if ($script:P.SYSTEM -ne 'recovery') { Die "The Brick did not restart into recovery (it reports: $($script:P.SYSTEM)). Run restore.bat again." }
    }
    'recovery' { }
    default { Die 'The Brick is running a system this installer does not recognise. Nothing was changed.' }
  }
  Step 'Writing the stock system (about a minute; keep the cable connected)'; Device restore-write
  Step 'Restarting into the stock system'; Adb shell 'reboot -f' | Out-Null
  Say ''; Say 'Done. The Brick is restarting into the stock TRIMUI system.'
  Say "Amalthea's saved sessions remain in internal storage (/mnt/UDISK/tracker-linux)."
}

try {
  if ($Action -eq 'restore') { Run-Restore } else { Run-Install }
  exit 0
} catch [StopInstaller] {
  Write-Host ''; Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
  exit 1
}
