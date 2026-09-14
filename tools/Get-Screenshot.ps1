#Requires -Version 7.0
<#
.SYNOPSIS
    Captures the Tab5's screen over the serial port into a PNG.

.DESCRIPTION
    Sends "screenshot" (protocol 6) and reassembles the SCREENSHOT / ROW / END
    lines the board answers with. The daemon must not hold the port: stop the
    scheduled task, or POST /release, first. Optional -Setup lines are written
    before the capture (states, session lines, "view detail <id>"...), -Wait
    seconds are waited after them (pulses, clocks), -Crop keeps a region of
    the capture and -Width scales the result.

.EXAMPLE
    .\tools\Get-Screenshot.ps1 -PortName COM7 -OutFile waiting.png -Setup 'session_clear','waiting_user' -Wait 3 -Crop 397,117,486,486 -Width 240
#>
param(
    [Parameter(Mandatory)][string]$PortName,
    [Parameter(Mandatory)][string]$OutFile,
    [string[]]$Setup = @(),
    [double]$Wait = 0,
    [int[]]$Crop,             # x, y, w, h of the capture to keep
    [int]$Width = 0,          # scale the result to this width (0 = as captured)
    [int]$TimeoutSeconds = 90
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# Base64 rows (R G B per pixel) -> one B G R buffer, the layout Format24bppRgb wants
Add-Type -TypeDefinition @'
using System;
public static class ShotRows {
    public static byte[] ToBgr(string[] rows, int w, int h) {
        var img = new byte[w * h * 3];
        for (int y = 0; y < h; y++) {
            if (rows[y] == null) continue;
            var rgb = Convert.FromBase64String(rows[y]);
            int n = Math.Min(rgb.Length, w * 3);
            int o = y * w * 3;
            for (int i = 0; i + 2 < n; i += 3) {
                img[o + i]     = rgb[i + 2];
                img[o + i + 1] = rgb[i + 1];
                img[o + i + 2] = rgb[i];
            }
        }
        return img;
    }
}
'@

$port = [System.IO.Ports.SerialPort]::new($PortName, 115200)
$port.DtrEnable = $false
$port.RtsEnable = $false
$port.NewLine = "`n"
$port.Encoding = [System.Text.Encoding]::ASCII
$port.ReadTimeout = 10000
$port.ReadBufferSize = 1MB
$port.Open()
try {
    $port.DiscardInBuffer()
    foreach ($line in $Setup) {
        $port.Write("$line`n")
        Start-Sleep -Milliseconds 60
    }
    if ($Wait -gt 0) { Start-Sleep -Milliseconds ([int]($Wait * 1000)) }

    $port.DiscardInBuffer()
    $port.Write("screenshot`n")
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $w = 0; $h = 0; $rows = $null; $got = 0
    while ((Get-Date) -lt $deadline) {
        $line = $port.ReadLine()
        if ($line -match '^SCREENSHOT w=(\d+) h=(\d+)') {
            $w = [int]$Matches[1]; $h = [int]$Matches[2]
            $rows = New-Object string[] $h
        } elseif ($rows -and $line -match '^ROW (\d+) (\S+)$') {
            $y = [int]$Matches[1]
            if ($y -lt $h) { $rows[$y] = $Matches[2]; $got++ }
        } elseif ($rows -and $line.StartsWith('END')) {
            break
        } elseif ($line.StartsWith('ERROR')) {
            throw $line
        }
    }
    if (-not $rows) { throw "no SCREENSHOT header from $PortName within $TimeoutSeconds s" }
    if ($got -lt $h) { Write-Warning "$($h - $got) of $h rows missing" }
} finally {
    $port.Close()
}

$bgr = [ShotRows]::ToBgr($rows, $w, $h)
$bmp = [System.Drawing.Bitmap]::new($w, $h, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$rect = [System.Drawing.Rectangle]::new(0, 0, $w, $h)
$data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bmp.PixelFormat)
try {
    for ($y = 0; $y -lt $h; $y++) {
        [System.Runtime.InteropServices.Marshal]::Copy($bgr, $y * $w * 3, [IntPtr]::Add($data.Scan0, $y * $data.Stride), $w * 3)
    }
} finally {
    $bmp.UnlockBits($data)
}

if ($Crop -and $Crop.Count -eq 4) {
    $c = [System.Drawing.Rectangle]::new($Crop[0], $Crop[1], $Crop[2], $Crop[3])
    $cropped = $bmp.Clone($c, $bmp.PixelFormat)
    $bmp.Dispose()
    $bmp = $cropped
}
if ($Width -gt 0 -and $Width -ne $bmp.Width) {
    $scaledH = [int][Math]::Round($bmp.Height * $Width / $bmp.Width)
    $scaled = [System.Drawing.Bitmap]::new($Width, $scaledH, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($scaled)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.DrawImage($bmp, 0, 0, $Width, $scaledH)
    $g.Dispose()
    $bmp.Dispose()
    $bmp = $scaled
}

$dir = Split-Path -Parent $OutFile
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
$bmp.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
"$OutFile ($($w)x$($h) captured)"
