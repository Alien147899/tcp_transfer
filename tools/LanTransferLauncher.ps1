Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$script:ProjectRoot = Split-Path -Parent $PSScriptRoot
$script:ExePath = Join-Path $script:ProjectRoot "build\Release\lan_transfer.exe"
$script:LastReceiver = $null

function Show-ErrorMessage {
    param([string]$Message)
    [System.Windows.Forms.MessageBox]::Show(
        $Message,
        "LAN Transfer",
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Error
    ) | Out-Null
}

function Show-InfoMessage {
    param([string]$Message)
    [System.Windows.Forms.MessageBox]::Show(
        $Message,
        "LAN Transfer",
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Information
    ) | Out-Null
}

function Read-JsonFile {
    param([string]$Path)
    if (-not (Test-Path $Path)) {
        return $null
    }
    try {
        return Get-Content $Path -Raw | ConvertFrom-Json
    } catch {
        return $null
    }
}

function Resolve-ExistingPathOrNull {
    param([string]$Path)
    try {
        $resolved = Resolve-Path $Path -ErrorAction Stop
        return $resolved.Path
    } catch {
        return $null
    }
}

function Refresh-Status {
    param(
        [System.Windows.Forms.Label]$DeviceLabel,
        [System.Windows.Forms.TextBox]$PairedDevicesBox,
        [System.Windows.Forms.Label]$ReceiverLabel
    )

    $deviceInfo = Read-JsonFile (Join-Path $script:ProjectRoot "device_info.json")
    if ($null -ne $deviceInfo) {
        $DeviceLabel.Text = "Local: $($deviceInfo.device_name) [$($deviceInfo.device_id)]"
    } else {
        $DeviceLabel.Text = "Local: not initialized"
    }

    $pairedInfo = Read-JsonFile (Join-Path $script:ProjectRoot "paired_devices.json")
    if ($null -ne $pairedInfo -and $null -ne $pairedInfo.devices -and $pairedInfo.devices.Count -gt 0) {
        $PairedDevicesBox.Text = ($pairedInfo.devices | ForEach-Object {
            "$($_.device_id)  $($_.host):$($_.port)"
        }) -join [Environment]::NewLine
    } else {
        $PairedDevicesBox.Text = "(no paired devices)"
    }

    if ($null -ne $script:LastReceiver -and -not $script:LastReceiver.HasExited) {
        $ReceiverLabel.Text = "Receiver: running"
        $ReceiverLabel.ForeColor = [System.Drawing.Color]::FromArgb(16, 112, 62)
    } else {
        $ReceiverLabel.Text = "Receiver: stopped"
        $ReceiverLabel.ForeColor = [System.Drawing.Color]::FromArgb(180, 40, 40)
    }
}

if (-not (Test-Path $script:ExePath)) {
    Show-ErrorMessage "Desktop executable not found:`n$script:ExePath`nBuild the project first."
    exit 1
}

$form = New-Object System.Windows.Forms.Form
$form.Text = "LAN Transfer Launcher"
$form.StartPosition = "CenterScreen"
$form.Size = New-Object System.Drawing.Size(760, 560)
$form.MinimumSize = New-Object System.Drawing.Size(760, 560)
$form.BackColor = [System.Drawing.Color]::FromArgb(248, 244, 235)

$title = New-Object System.Windows.Forms.Label
$title.Text = "LAN Transfer Control Panel"
$title.Font = New-Object System.Drawing.Font("Segoe UI", 18, [System.Drawing.FontStyle]::Bold)
$title.AutoSize = $true
$title.Location = New-Object System.Drawing.Point(24, 20)
$form.Controls.Add($title)

$subtitle = New-Object System.Windows.Forms.Label
$subtitle.Text = "Start receiver, discover devices, and open the download folder."
$subtitle.AutoSize = $true
$subtitle.Font = New-Object System.Drawing.Font("Segoe UI", 10)
$subtitle.Location = New-Object System.Drawing.Point(28, 58)
$form.Controls.Add($subtitle)

$deviceLabel = New-Object System.Windows.Forms.Label
$deviceLabel.AutoSize = $true
$deviceLabel.Font = New-Object System.Drawing.Font("Consolas", 10)
$deviceLabel.Location = New-Object System.Drawing.Point(28, 96)
$form.Controls.Add($deviceLabel)

$receiverLabel = New-Object System.Windows.Forms.Label
$receiverLabel.AutoSize = $true
$receiverLabel.Font = New-Object System.Drawing.Font("Segoe UI", 10, [System.Drawing.FontStyle]::Bold)
$receiverLabel.Location = New-Object System.Drawing.Point(28, 122)
$form.Controls.Add($receiverLabel)

$portLabel = New-Object System.Windows.Forms.Label
$portLabel.Text = "Listen Port"
$portLabel.AutoSize = $true
$portLabel.Location = New-Object System.Drawing.Point(30, 165)
$form.Controls.Add($portLabel)

$portBox = New-Object System.Windows.Forms.TextBox
$portBox.Text = "9000"
$portBox.Location = New-Object System.Drawing.Point(30, 188)
$portBox.Size = New-Object System.Drawing.Size(140, 28)
$form.Controls.Add($portBox)

$downloadLabel = New-Object System.Windows.Forms.Label
$downloadLabel.Text = "Download Folder"
$downloadLabel.AutoSize = $true
$downloadLabel.Location = New-Object System.Drawing.Point(190, 165)
$form.Controls.Add($downloadLabel)

$downloadBox = New-Object System.Windows.Forms.TextBox
$downloadBox.Text = ".\downloads"
$downloadBox.Location = New-Object System.Drawing.Point(190, 188)
$downloadBox.Size = New-Object System.Drawing.Size(390, 28)
$form.Controls.Add($downloadBox)

$browseButton = New-Object System.Windows.Forms.Button
$browseButton.Text = "Browse..."
$browseButton.Location = New-Object System.Drawing.Point(595, 184)
$browseButton.Size = New-Object System.Drawing.Size(100, 34)
$form.Controls.Add($browseButton)

$startReceiverButton = New-Object System.Windows.Forms.Button
$startReceiverButton.Text = "Start Receiver"
$startReceiverButton.Location = New-Object System.Drawing.Point(30, 240)
$startReceiverButton.Size = New-Object System.Drawing.Size(150, 40)
$form.Controls.Add($startReceiverButton)

$discoverButton = New-Object System.Windows.Forms.Button
$discoverButton.Text = "Discover"
$discoverButton.Location = New-Object System.Drawing.Point(195, 240)
$discoverButton.Size = New-Object System.Drawing.Size(150, 40)
$form.Controls.Add($discoverButton)

$openDownloadsButton = New-Object System.Windows.Forms.Button
$openDownloadsButton.Text = "Open Downloads"
$openDownloadsButton.Location = New-Object System.Drawing.Point(360, 240)
$openDownloadsButton.Size = New-Object System.Drawing.Size(150, 40)
$form.Controls.Add($openDownloadsButton)

$openProjectButton = New-Object System.Windows.Forms.Button
$openProjectButton.Text = "Open Project"
$openProjectButton.Location = New-Object System.Drawing.Point(525, 240)
$openProjectButton.Size = New-Object System.Drawing.Size(150, 40)
$form.Controls.Add($openProjectButton)

$pairedLabel = New-Object System.Windows.Forms.Label
$pairedLabel.Text = "Paired Devices"
$pairedLabel.AutoSize = $true
$pairedLabel.Font = New-Object System.Drawing.Font("Segoe UI", 10, [System.Drawing.FontStyle]::Bold)
$pairedLabel.Location = New-Object System.Drawing.Point(30, 305)
$form.Controls.Add($pairedLabel)

$pairedDevicesBox = New-Object System.Windows.Forms.TextBox
$pairedDevicesBox.Location = New-Object System.Drawing.Point(30, 332)
$pairedDevicesBox.Size = New-Object System.Drawing.Size(665, 88)
$pairedDevicesBox.Multiline = $true
$pairedDevicesBox.ScrollBars = "Vertical"
$pairedDevicesBox.ReadOnly = $true
$pairedDevicesBox.Font = New-Object System.Drawing.Font("Consolas", 10)
$form.Controls.Add($pairedDevicesBox)

$logLabel = New-Object System.Windows.Forms.Label
$logLabel.Text = "Log"
$logLabel.AutoSize = $true
$logLabel.Font = New-Object System.Drawing.Font("Segoe UI", 10, [System.Drawing.FontStyle]::Bold)
$logLabel.Location = New-Object System.Drawing.Point(30, 434)
$form.Controls.Add($logLabel)

$logBox = New-Object System.Windows.Forms.TextBox
$logBox.Location = New-Object System.Drawing.Point(30, 460)
$logBox.Size = New-Object System.Drawing.Size(665, 50)
$logBox.Multiline = $true
$logBox.ScrollBars = "Vertical"
$logBox.ReadOnly = $true
$logBox.Font = New-Object System.Drawing.Font("Consolas", 10)
$form.Controls.Add($logBox)

function Add-Log {
    param([string]$Message)
    $timestamp = Get-Date -Format "HH:mm:ss"
    $entry = "[$timestamp] $Message"
    if ([string]::IsNullOrWhiteSpace($logBox.Text)) {
        $logBox.Text = $entry
    } else {
        $logBox.Text = "$entry$([Environment]::NewLine)$($logBox.Text)"
    }
}

$browseButton.Add_Click({
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $existing = Resolve-ExistingPathOrNull (Join-Path $script:ProjectRoot $downloadBox.Text)
    if ($null -ne $existing) {
        $dialog.SelectedPath = $existing
    }
    if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
        $downloadBox.Text = $dialog.SelectedPath
    }
})

$startReceiverButton.Add_Click({
    $port = $portBox.Text.Trim()
    $downloadDir = $downloadBox.Text.Trim()
    if ([string]::IsNullOrWhiteSpace($port) -or [string]::IsNullOrWhiteSpace($downloadDir)) {
        Show-ErrorMessage "Listen port and download folder are required."
        return
    }

    try {
        $absoluteDownload = [System.IO.Path]::GetFullPath((Join-Path $script:ProjectRoot $downloadDir))
        New-Item -ItemType Directory -Force -Path $absoluteDownload | Out-Null

        $command = "cd /d `"$script:ProjectRoot`" && `"$script:ExePath`" receive $port `"$absoluteDownload`""
        $script:LastReceiver = Start-Process -FilePath "cmd.exe" -ArgumentList "/k", $command -PassThru
        Add-Log "Receiver started on port $port, folder $absoluteDownload"
        Refresh-Status -DeviceLabel $deviceLabel -PairedDevicesBox $pairedDevicesBox -ReceiverLabel $receiverLabel
    } catch {
        Show-ErrorMessage "Failed to start receiver: $($_.Exception.Message)"
    }
})

$discoverButton.Add_Click({
    $port = $portBox.Text.Trim()
    if ([string]::IsNullOrWhiteSpace($port)) {
        Show-ErrorMessage "Enter a listen port first."
        return
    }

    try {
        $output = & $script:ExePath discover $port 5 2>&1 | Out-String
        Add-Log "Discovery finished."
        Show-InfoMessage $output.Trim()
        Refresh-Status -DeviceLabel $deviceLabel -PairedDevicesBox $pairedDevicesBox -ReceiverLabel $receiverLabel
    } catch {
        Show-ErrorMessage "Discovery failed: $($_.Exception.Message)"
    }
})

$openDownloadsButton.Add_Click({
    try {
        $absoluteDownload = [System.IO.Path]::GetFullPath((Join-Path $script:ProjectRoot $downloadBox.Text.Trim()))
        New-Item -ItemType Directory -Force -Path $absoluteDownload | Out-Null
        Start-Process explorer.exe $absoluteDownload
        Add-Log "Opened download folder."
    } catch {
        Show-ErrorMessage "Failed to open download folder: $($_.Exception.Message)"
    }
})

$openProjectButton.Add_Click({
    Start-Process explorer.exe $script:ProjectRoot
    Add-Log "Opened project folder."
})

$form.Add_Shown({
    Refresh-Status -DeviceLabel $deviceLabel -PairedDevicesBox $pairedDevicesBox -ReceiverLabel $receiverLabel
    Add-Log "Launcher ready."
})

[void]$form.ShowDialog()
