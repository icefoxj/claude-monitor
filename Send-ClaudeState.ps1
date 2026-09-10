param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("processing", "waiting_user", "idle", "off")]
    [string]$State, [string]$PortName="COM5"
)

$port = New-Object System.IO.Ports.SerialPort $PortName, 115200
$port.NewLine = "`n"
$port.DtrEnable = $false
$port.RtsEnable = $false
$port.WriteTimeout = 1000

try{
    $port.Open()
    $port.WriteLine($State)
}
catch{

}
finally{
    if ($port.IsOpen) {$port.Close()}
    $port.Dispose()
}

exit 0