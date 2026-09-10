param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("processing", "waiting_user", "question", "error", "paused", "compacting",
                 "idle", "off", "subagent_start", "subagent_stop")]
    [string]$State,

    [string]$PortName = "COM5",
    [int]$DaemonPort = 47831
)

# If the daemon is running it owns the port: hand the state to it instead
try {
    Invoke-RestMethod -Uri "http://localhost:$DaemonPort/state/$State" -Method Post -TimeoutSec 1 | Out-Null
    exit 0
} catch {}

# No daemon: write to the port ourselves
$port = New-Object System.IO.Ports.SerialPort $PortName, 115200
$port.NewLine       = "`n"     # WriteLine will terminate with \n, not \r\n
$port.DtrEnable     = $false   # removes any chance of an accidental reset
$port.RtsEnable     = $false
$port.WriteTimeout  = 1000     # ms; never leaves a hook hanging

# Two hooks can fire within milliseconds of each other (e.g. PostToolUse
# and then PreToolUse) and the port is exclusive-access on Windows, so a
# busy port is retried briefly instead of dropping the command
$attempts = 5
for ($i = 1; $i -le $attempts; $i++) {
    try {
        $port.Open()
        $port.WriteLine($State)
        break
    }
    catch {
        # Silent on purpose: a disconnected device or a busy port
        # must never become a hook failure
        if ($i -lt $attempts) { Start-Sleep -Milliseconds 100 }
    }
    finally {
        if ($port.IsOpen) { $port.Close() }
    }
}
$port.Dispose()

exit 0   # the "|| true" equivalent: hooks treat exit codes as semantics
