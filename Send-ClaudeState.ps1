param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("processing", "waiting_user", "question", "idle", "off")]
    [string]$State,

    [string]$PortName = "COM5"
)

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
