#Requires -Version 7.0
<#
.SYNOPSIS
    claude-monitor host daemon.

.DESCRIPTION
    Keeps the device's serial port permanently open and turns Claude Code hook
    events, received as HTTP POSTs on localhost, into device states. Compared
    with one process per hook this has no start-up latency, a single writer on
    the port, guaranteed ordering, and a log with timestamps.

    Every Claude Code session is tracked separately; the device shows the most
    urgent state among the live sessions (error > waiting_user = question >
    paused > compacting > processing > idle), so a second session cannot hide a
    permission prompt from the first one. The device also gets the list of
    live sessions ("sessions pwi", one letter each) for its session dots, and
    a "ping" every PingSeconds so it can tell when the host is gone.

    A session whose Claude Code was killed without a SessionEnd would stay
    for SessionTimeoutMinutes; a processing/compacting session whose
    transcript file has not changed for DeadSessionMinutes is dropped early.

    A compaction ends at the SubagentStop of the subagent that runs it (one
    that never sent SubagentStart): the session goes back to the state the
    PreCompact interrupted. That is the only end signal when Claude Code
    compacts a session left idle (54 minutes after its Stop), whose
    PostCompact comes with the next prompt; CompactTimeoutMinutes without
    events ends a compaction whose stop never came.

    Claude Code has no hook for the moment a permission is approved. The
    daemon learns each session's claude.exe from the hook's TCP connection
    and watches its child processes in snapshots of the process table: a
    tool process starting under a waiting session means "approved"
    (processing at once, not when the tool ends), a tool process alive means
    "tool running" (tool_start / tool_stop, a blue EXT badge on the device),
    and the session's process going away means the session is dead.

    A device whose VERSION lists "sessions" in features= (the Tab5) shows
    one tile per live session: it gets a "session <id> label=.. state=..
    subagents=.. tool=.." line whenever a session changes, "session_end"
    when one leaves, and "session_clear" followed by all of them when the
    port is reopened. The cube keeps getting the aggregate words.

    Every hook event, with all its fields, is also forwarded as one "event"
    line to a device whose VERSION lists "events" in features= (the Tab5's
    hook inspector); the AtomS3R never receives them. The line also names
    the session's project: the hooks send the X-Claude-Project header
    (${CLAUDE_PROJECT_DIR}), else the first cwd in the session's
    transcript is taken; its folder name is used unless ProjectsPath
    (projects.json, { "root": "name" }) maps it to a nicer one. The last 50 are kept
    for GET /events, and with -LogEvents each raw payload is appended as
    one JSON line to EventLogPath (prompts and tool inputs included: mind
    what ends up in that file).

    The account's plan usage (the 5-hour session, the week, per-model weeks,
    what /usage shows) goes to a device whose features= lists "usage" (the
    Tab5's screensaver) as one "usage" line whenever it changes. It comes
    from the status line, when one is set up to POST its input to
    /statusline (documented, but only terminal sessions run a status line,
    not the VS Code extension), and with -AccountUsage from the endpoint
    /usage itself reads, polled every UsagePollSeconds with the access token
    Claude Code keeps in CredentialsPath. That endpoint is undocumented and
    may change; the token is only sent to api.anthropic.com, never logged.

    The same line carries what the Tab5's usage page shows (a tap on its
    usage tile): the host's clock and each limit's window, for the pace and
    the reset times; the plan (from the login, then the account's profile,
    read every six hours) and its extra usage; the week so far, hour by
    hour, from the readings kept in UsageHistoryPath (a line per change,
    else one an hour; seeded from the log the first time); and Claude Code's
    share of the weekly limit per project, estimated from the token counts
    in Claude Code's own transcripts (TranscriptsPath, read incrementally
    by a C# helper, a minute apart; -NoProjectUsage turns that off).

    The Claude desktop app's Code tab fires the same hooks as the CLI, but
    Cowork and the app's scheduled tasks fire none: the app tells you
    through Windows notifications ("Scheduled task needs your approval").
    With -DesktopNotifications the daemon reads those, read-only, from the
    database Windows keeps for the Action Center (wpndatabase.db, through
    the system's winsqlite3.dll) and shows them as one more session,
    "Claude Desktop": the red sign for one that needs you, the green check
    for the others. Windows does not say when you acted on it, so the red
    sign goes when the notification leaves the Action Center, when the
    app's window has been in front for three seconds, or after
    DesktopWaitMinutes; the tile itself goes DesktopLingerMinutes later.

    Endpoints (all on http://localhost:<HttpPort>/):
      POST /hook            Claude Code hook input JSON (any event)
      POST /statusline      Claude Code status line input JSON; answers with the text to show
      GET  /usage           the plan usage last read, where from, the polling state, plan, extra
                            usage, the week's readings and the usage by project
      GET  /events[?n=20]   the last hook events as the device sees them (tag, line, time)
      POST /state/<state>   write one state straight to the device (tests)
      GET  /serial/<cmd>    write any protocol word; for "status" / "version" returns the reply
      GET  /version         ask the device who it is: board, model, chip, firmware, ESP-IDF, build, uptime, last reset
      POST /calibrate?rot=<0-3>&sign=<1|-1>&offset=<deg>   store the orientation on the device; ?reset=1 clears it
      GET  /status          daemon state, sessions, device state
      POST /release[?seconds=120]   close the port so idf.py can flash; reopens after the delay
      POST /reconnect       reopen the port now
      GET  /health

.EXAMPLE
    pwsh -File Monitor-Daemon.ps1 -PortName COM5
#>
[CmdletBinding()]
param(
    [string]$PortName = "COM5",
    [int]$HttpPort = 47831,
    [string]$LogPath = (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) "claude-monitor\daemon.log"),
    [int]$SessionTimeoutMinutes = 240,
    [int]$DeadSessionMinutes = 15,
    [int]$CompactTimeoutMinutes = 10,
    [int]$PingSeconds = 30,
    [switch]$LogEvents,
    [string]$EventLogPath = (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) "claude-monitor\hooks.jsonl"),
    [int]$EventValueMax = 160,
    [string]$ProjectsPath = (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) "claude-monitor\projects.json"),
    [switch]$AccountUsage,            # poll the plan usage the way /usage does, with Claude Code's login
    [int]$UsagePollSeconds = 300,
    [string]$CredentialsPath = (Join-Path (Join-Path $HOME '.claude') '.credentials.json'),
    [string]$UsageHistoryPath = (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) "claude-monitor\usage-history.jsonl"),
    [string]$TranscriptsPath = (Join-Path (Join-Path $HOME '.claude') 'projects'),
    [switch]$NoProjectUsage,          # do not read the transcripts for the usage by project
    [switch]$DesktopNotifications,    # show the Claude desktop app's notifications (Cowork, scheduled tasks) as a session
    [int]$DesktopWaitMinutes = 30,
    [int]$DesktopLingerMinutes = 10
)

$ErrorActionPreference = 'Continue'

$States   = @('processing', 'waiting_user', 'question', 'error', 'paused', 'compacting', 'idle', 'off')
$Priority = @{ off = 0; idle = 1; processing = 2; compacting = 3; paused = 4; question = 5; waiting_user = 5; error = 6 }
$Code     = @{ processing = 'p'; waiting_user = 'w'; question = 'q'; error = 'e'; paused = 'h'; compacting = 'c'; idle = 'i' }

# ---------------- logging ----------------

New-Item -ItemType Directory -Force (Split-Path $LogPath) | Out-Null

function Log([string]$msg) {
    $line = "{0:yyyy-MM-dd HH:mm:ss.fff} {1}" -f (Get-Date), $msg
    try {
        if ((Test-Path $LogPath) -and (Get-Item $LogPath).Length -gt 5MB) {
            Move-Item -Force $LogPath "$LogPath.1"
        }
        Add-Content -Path $LogPath -Value $line -Encoding utf8
    } catch {}
    Write-Host $line
}

# ---------------- serial port ----------------

$script:serial          = $null
$script:releaseUntil    = [datetime]::MinValue
$script:nextOpenTry     = [datetime]::MinValue
$script:deviceState     = $null   # last state written to the device
$script:deviceSubagents = 0       # subagent count the device currently holds
$script:deviceSessions  = $null   # session codes the device currently shows
$script:deviceTool      = $false  # the device shows the tool-running (EXT) badge
$script:lastPing        = [datetime]::MinValue
$script:deviceInfo      = $null   # fields of the device's VERSION line, read when the port opens
$script:lastInfoTry     = [datetime]::MinValue
$script:started         = Get-Date

function Open-Serial {
    try {
        $p = [System.IO.Ports.SerialPort]::new($PortName, 115200)
        $p.NewLine      = "`n"
        $p.Encoding     = [System.Text.Encoding]::UTF8   # event lines carry whatever the hooks carried
        $p.DtrEnable    = $false   # never reset the board on open
        $p.RtsEnable    = $false
        $p.WriteTimeout = 500
        $p.ReadTimeout  = 200
        $p.Open()
        $script:serial = $p
        Log "serial: opened $PortName"
        return $true
    } catch {
        $script:serial = $null
        return $false
    }
}

function Close-Serial([string]$why) {
    if ($script:serial) {
        try { $script:serial.Close(); $script:serial.Dispose() } catch {}
        $script:serial = $null
        Log "serial: closed ($why)"
    }
}

function Send-Serial([string]$line) {
    if (-not $script:serial) { return $false }
    try {
        $script:serial.WriteLine($line)
        return $true
    } catch {
        Log "serial: write failed: $($_.Exception.Message)"
        Close-Serial "write error"
        return $false
    }
}

function Read-SerialLines {
    if (-not $script:serial) { return @() }
    try {
        if ($script:serial.BytesToRead -gt 0) {
            return ($script:serial.ReadExisting() -split "`n")
        }
    } catch {
        Close-Serial "read error"
    }
    return @()
}

# Writes a protocol line; for "status", "version" and "calibrate" waits
# briefly for the device's reply (a STATUS or VERSION line, or ERROR for a
# rejected calibration)
function Send-SerialCommand([string]$cmd) {
    $expect = switch -Wildcard ($cmd) {
        'status'     { 'STATUS' }
        'version'    { 'VERSION' }
        'calibrate*' { 'STATUS|ERROR' }
        default      { $null }
    }
    if (-not (Send-Serial $cmd)) { return $null }
    if (-not $expect) { return $null }
    $deadline = (Get-Date).AddSeconds(1.5)
    $buf = ''
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 50
        try {
            if ($script:serial -and $script:serial.BytesToRead -gt 0) { $buf += $script:serial.ReadExisting() }
        } catch {}
        if ($buf -match "($expect)[^\r\n]*") { return $Matches[0] }
    }
    return $null
}

# "VERSION board=atoms3r fw=v1.2.0 ..." -> ordered hashtable of its fields
function ConvertFrom-KvLine([string]$line) {
    $fields = [ordered]@{}
    foreach ($tok in ($line -split ' ') | Select-Object -Skip 1) {
        $i = $tok.IndexOf('=')
        if ($i -gt 0) { $fields[$tok.Substring(0, $i)] = $tok.Substring($i + 1) }
    }
    return $fields
}

# Asks the device who it is; keeps the answer for /status and logs it
function Read-DeviceInfo {
    $reply = Send-SerialCommand 'version'
    if ($reply -and $reply -like 'VERSION *') {
        $script:deviceInfo = ConvertFrom-KvLine $reply
        Log "device: $reply"
    } else {
        $script:deviceInfo = $null
        Log "device: no VERSION reply (firmware older than 1.3?)"
    }
    return $reply
}

# ---------------- projects ----------------
#
# The project a session belongs to comes from the X-Claude-Project header
# the hooks send (${CLAUDE_PROJECT_DIR}, the root where the session
# started); without the header, the first cwd in the session's transcript,
# else the cwd of the first event seen. The name
# shown is the root's folder name unless projects.json maps the root to a
# nicer one: { "E:\\work\\claude-monitor": "Monitor" }.

$script:projectNames  = @{}
$script:projectsMtime = [datetime]::MinValue

function ConvertTo-RootKey([string]$root) {
    return ($root -replace '[\\/]+$', '').ToLowerInvariant()
}

# Reloads projects.json when it changed (called at start and by housekeeping)
function Update-ProjectNames {
    try {
        if (-not (Test-Path $ProjectsPath)) {
            if ($script:projectNames.Count) { $script:projectNames = @{}; Log "projects: $ProjectsPath removed" }
            return
        }
        $m = (Get-Item $ProjectsPath).LastWriteTime
        if ($m -eq $script:projectsMtime) { return }
        $obj = Get-Content $ProjectsPath -Raw | ConvertFrom-Json
        $map = @{}
        foreach ($p in $obj.PSObject.Properties) { $map[(ConvertTo-RootKey $p.Name)] = [string]$p.Value }
        $script:projectNames  = $map
        $script:projectsMtime = $m
        Log "projects: $($map.Count) name(s) from $ProjectsPath"
    } catch {
        Log "projects: cannot read ${ProjectsPath}: $($_.Exception.Message)"
    }
}

function Get-ProjectName([string]$root) {
    if (-not $root) { return $null }
    $key = ConvertTo-RootKey $root
    if ($script:projectNames.ContainsKey($key)) { return $script:projectNames[$key] }
    return Split-Path -Leaf ($root -replace '[\\/]+$', '')
}

# Where a session was started: the first cwd in its transcript (the one the
# transcript reader also takes as the project). Read once, when a session
# is first seen without the header; the start of the file is enough.
function Get-TranscriptRoot([string]$path) {
    if (-not $path) { return $null }
    try {
        $fs = [IO.FileStream]::new($path, 'Open', 'Read', [IO.FileShare]'ReadWrite, Delete')
        try {
            $buf = [byte[]]::new(1MB)
            $n = 0
            while ($n -lt $buf.Length) {
                $r = $fs.Read($buf, $n, $buf.Length - $n)
                if ($r -le 0) { break }
                $n += $r
            }
        } finally { $fs.Dispose() }
        $m = [regex]::Match([Text.Encoding]::UTF8.GetString($buf, 0, $n), '"cwd":"((?:[^"\\]|\\.)*)"')
        if ($m.Success) { return [string](ConvertFrom-Json ('"' + $m.Groups[1].Value + '"')) }
    } catch {}
    return $null
}

# ---------------- Claude Code processes ----------------
#
# There is no hook for the moment a permission is approved: the red sign
# would stay until the tool finished. But the tool runs as a child process
# of the session's claude.exe, and that process is known: the hook's HTTP
# connection names it (Get-NetTCPConnection, once per session). A snapshot
# of the process table, taken by the main loop, then shows a tool starting
# under it (= approved, and "tool running" for the EXT badge), ending, and
# the session's own process going away.
#
# The snapshot is NtQuerySystemInformation(SystemProcessInformation) through
# a small C# helper (about 20 ms for 700 processes), filtered to the
# sessions' pids before it reaches PowerShell; twice a second while a
# session waits for an approval or has just called a tool, every two seconds
# otherwise. WMI __InstanceCreationEvent polling was used first and is a
# trap: the arbitrator keeps whole Win32_Process instances for a polling
# query within a 5 MB per-user quota, so with a few hundred processes (an
# ESP-IDF build) the subscription is cancelled without a word and new ones
# fail with "Quota violation"; and the flood of events before that stalled
# the daemon for minutes, with the hooks timing out meanwhile.

$script:procSnap   = $false               # the snapshot helper is available (Windows only)
$script:procNextAt = [datetime]::MinValue # next snapshot
$IgnoredChildren = @('conhost.exe')

# x64 layout of SYSTEM_PROCESS_INFORMATION: CreateTime at 32, ImageName
# (UNICODE_STRING) at 56, UniqueProcessId at 80, InheritedFromUniqueProcessId at 88
$ProcSnapSource = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class ProcSnap {
    [DllImport("ntdll.dll")] static extern int NtQuerySystemInformation(int cls, IntPtr buf, int len, out int retLen);
    public struct Entry { public int Pid; public int Parent; public string Name; public long Created; }
    static IntPtr buf = IntPtr.Zero; static int bufLen = 0;
    // Processes whose pid or parent pid is in `parents`
    public static List<Entry> Take(int[] parents) {
        int need;
        if (buf == IntPtr.Zero) { bufLen = 1 << 20; buf = Marshal.AllocHGlobal(bufLen); }
        int status = NtQuerySystemInformation(5, buf, bufLen, out need);   // SystemProcessInformation
        while (status == unchecked((int)0xC0000004)) {                      // STATUS_INFO_LENGTH_MISMATCH
            Marshal.FreeHGlobal(buf); bufLen = need + (256 << 10); buf = Marshal.AllocHGlobal(bufLen);
            status = NtQuerySystemInformation(5, buf, bufLen, out need);
        }
        if (status != 0) return null;
        var list = new List<Entry>();
        long p = buf.ToInt64();
        while (true) {
            int next = Marshal.ReadInt32((IntPtr)p, 0);
            int pid = (int)Marshal.ReadIntPtr((IntPtr)p, 80).ToInt64();
            int parent = (int)Marshal.ReadIntPtr((IntPtr)p, 88).ToInt64();
            if (Array.IndexOf(parents, parent) >= 0 || Array.IndexOf(parents, pid) >= 0) {
                int nameLen = Marshal.ReadInt16((IntPtr)p, 56);
                IntPtr namePtr = Marshal.ReadIntPtr((IntPtr)p, 64);
                string name = (namePtr != IntPtr.Zero && nameLen > 0) ? Marshal.PtrToStringUni(namePtr, nameLen / 2) : "";
                list.Add(new Entry { Pid = pid, Parent = parent, Name = name, Created = Marshal.ReadInt64((IntPtr)p, 32) });
            }
            if (next == 0) break;
            p += next;
        }
        return list;
    }
}
'@

function Initialize-ProcessSnapshots {
    if (-not $IsWindows) { Log "processes: not Windows, approval detection off"; return }
    try {
        Add-Type -TypeDefinition $ProcSnapSource -ErrorAction Stop
        $null = [ProcSnap]::Take([int[]]@(0))
        $script:procSnap = $true
        Log "processes: watching the sessions' child processes (approval detection on)"
    } catch {
        Log "processes: snapshot helper unavailable, approval detection off: $($_.Exception.Message)"
    }
}

function Get-SessionPids {
    return @($script:sessions.Values | Where-Object { $_.pid -gt 0 } | ForEach-Object { [int]$_.pid } | Sort-Object -Unique)
}

function Note-ToolStarted($s, [string]$name, [int]$childPid) {
    $s.tools[$childPid] = $name
    if ($s.state -eq 'waiting_user') {
        $s.state = 'processing'
        $s.last  = Get-Date
        Log "session $($s.short): tool $name started under pid $($s.pid): approved -> processing"
    } else {
        Log "session $($s.short): tool $name started (pid $childPid)"
    }
}

# Compares the live children of each session's process with what it had: a
# new child is a tool starting (the approval, when the session was waiting),
# a missing one a tool that ended, a missing session process a dead session.
# The first look after a session's pid becomes known only records what is
# already running: that predates any prompt. Returns true when something the
# device shows may have changed.
function Update-ProcessTree {
    if (-not $script:procSnap) { return $false }
    $pids = Get-SessionPids
    if ($pids.Count -eq 0) { return $false }
    $entries = $null
    try { $entries = [ProcSnap]::Take([int[]]$pids) } catch { return $false }
    if ($null -eq $entries) { return $false }
    $alive    = @{}
    $children = @{}
    foreach ($e in $entries) {
        $alive[[int]$e.Pid] = $true
        if ([string]$e.Name -in $IgnoredChildren) { continue }
        if (-not $children.ContainsKey([int]$e.Parent)) { $children[[int]$e.Parent] = @{} }
        $children[[int]$e.Parent][[int]$e.Pid] = [string]$e.Name
    }
    $changed = $false
    foreach ($s in @($script:sessions.Values)) {
        if ($s.pid -le 0) { continue }
        if (-not $alive.ContainsKey([int]$s.pid)) {
            if (-not $s.dead) { $s.dead = $true; $changed = $true }
            continue
        }
        $live = if ($children.ContainsKey([int]$s.pid)) { $children[[int]$s.pid] } else { @{} }
        foreach ($cpid in @($live.Keys)) {
            if ($s.tools.ContainsKey($cpid)) { continue }
            if ($s.seeded) {
                Note-ToolStarted $s $live[$cpid] $cpid
            } else {
                $s.tools[$cpid] = $live[$cpid]
                Log "session $($s.short): tool $($live[$cpid]) already running (pid $cpid)"
            }
            $changed = $true
        }
        foreach ($t in @($s.tools.Keys)) {
            if (-not $live.ContainsKey($t)) {
                Log "session $($s.short): tool $($s.tools[$t]) ended (pid $t)"
                $s.tools.Remove($t)
                $changed = $true
            }
        }
        $s.seeded = $true
    }
    return $changed
}

# The process on the other end of a hook request: the claude.exe of the
# session. Only valid while the request is being handled (the connection
# is closed right after the response).
function Resolve-ClientPid($req) {
    try {
        $port = [int]$req.RemoteEndPoint.Port
        $c = Get-NetTCPConnection -LocalPort $port -RemotePort $HttpPort -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($c -and $c.OwningProcess -gt 0) { return [int]$c.OwningProcess }
    } catch {}
    return 0
}

function Test-ProcessAlive([int]$processId) {
    if ($processId -le 0) { return $false }
    return $null -ne (Get-Process -Id $processId -ErrorAction SilentlyContinue)
}

# ---------------- sessions ----------------

$script:sessions = @{}   # session_id -> @{ state; subagents; last; transcript; root; project; pid; tools; dead; short }

function Get-Session([string]$id) {
    if (-not $script:sessions.ContainsKey($id)) {
        $script:sessions[$id] = @{
            id = $id; state = 'idle'; subagents = 0; last = (Get-Date); first = (Get-Date); transcript = $null; root = $null; project = $null
            beforeCompact = 'idle'   # the state a compaction interrupted, restored when it ends
            pid = 0; tools = @{}; seeded = $false; dead = $false; short = $id.Substring(0, [Math]::Min(8, $id.Length))
        }
    }
    return $script:sessions[$id]
}

# What a tile is called: the project name, "#n" appended when several
# live sessions share the project (numbered by age, so the numbers hold)
function Get-SessionLabel($s) {
    $name = if ($s.project) { [string]$s.project } else { [string]$s.short }
    if (-not $s.project) { return $name }
    $same = @($script:sessions.Values | Where-Object { $_.project -eq $s.project } | Sort-Object -Property first)
    if ($same.Count -gt 1) {
        for ($i = 0; $i -lt $same.Count; $i++) {
            if ($same[$i].id -eq $s.id) { return "$name #$($i + 1)" }
        }
    }
    return $name
}

# One line per live session for a board that shows tiles
function Format-SessionLine([string]$id, $s) {
    $tool = if ($s.tools.Count -gt 0) { 1 } else { 0 }
    return "session $id`tlabel=$(Format-EventValue (Get-SessionLabel $s))`tstate=$($s.state)`tsubagents=$($s.subagents)`ttool=$tool`tproject=$(Format-EventValue $s.project)`troot=$(Format-EventValue $s.root)"
}

$script:deviceSessionLines = @{}   # session id -> the line the device last got

# Keeps a "sessions"-capable device's tiles equal to the live sessions:
# session_end for the ones that left, a session line for each that changed.
# `force` starts from session_clear (the port was just reopened).
function Send-SessionLines([bool]$force) {
    if (-not $script:serial -or -not $script:deviceInfo) { return }
    if (([string]$script:deviceInfo.features) -notmatch '(^|,)sessions(,|$)') { return }
    if ($force) {
        if (Send-Serial 'session_clear') { Log 'device <- session_clear' }
        $script:deviceSessionLines = @{}
    }
    foreach ($id in @($script:deviceSessionLines.Keys)) {
        if (-not $script:sessions.ContainsKey($id)) {
            if (Send-Serial "session_end $id") {
                Log "device <- session_end $id"
                $script:deviceSessionLines.Remove($id)
            }
        }
    }
    foreach ($id in @($script:sessions.Keys)) {
        $line = Format-SessionLine $id $script:sessions[$id]
        if ($script:deviceSessionLines[$id] -ne $line) {
            if (Send-Serial $line) {
                Log "device <- $($line -replace "`t", ' ')"
                $script:deviceSessionLines[$id] = $line
            }
        }
    }
}

# Drops sessions that are silent for too long, and processing/compacting
# sessions whose transcript stopped changing: Claude Code appends to it
# while it works, so a stale one means the process is gone (or a tool call
# has run for longer than DeadSessionMinutes, in which case the next hook
# event simply recreates the session)
function Expire-Sessions {
    $now  = Get-Date
    $cut  = $now.AddMinutes(-$SessionTimeoutMinutes)
    $dead = $now.AddMinutes(-$DeadSessionMinutes)
    foreach ($id in @($script:sessions.Keys)) {
        $s = $script:sessions[$id]
        if ($s.last -lt $cut) {
            $script:sessions.Remove($id)
            Log "session ${id}: expired after $SessionTimeoutMinutes min without events"
            continue
        }
        # The exact signal when the process is known: it is gone
        if ($s.dead -or ($s.pid -gt 0 -and -not (Test-ProcessAlive $s.pid))) {
            $script:sessions.Remove($id)
            Log "session ${id}: dropped, its Claude Code process (pid $($s.pid)) is gone"
            continue
        }
        # A compaction whose end never arrived (it takes one to three minutes)
        if ($s.state -eq 'compacting' -and $s.last -lt $now.AddMinutes(-$CompactTimeoutMinutes)) {
            $s.state = $s.beforeCompact
            Log "session ${id}: compacting for $CompactTimeoutMinutes min without events, back to $($s.state)"
        }
        if ($s.pid -le 0 -and $s.state -in 'processing', 'compacting' -and $s.transcript -and $s.last -lt $dead) {
            $stale = $false
            try {
                if (-not (Test-Path -LiteralPath $s.transcript)) { $stale = $true }
                elseif ((Get-Item -LiteralPath $s.transcript).LastWriteTime -lt $dead) { $stale = $true }
            } catch {}
            if ($stale) {
                $script:sessions.Remove($id)
                Log "session ${id}: dropped, $($s.state) but its transcript has not changed for $DeadSessionMinutes min"
            }
        }
    }
}

# One code letter per live session, most urgent first, then most recent
function Get-SessionCodes {
    $ordered = $script:sessions.Values | Sort-Object -Property @{ Expression = { $Priority[$_.state] }; Descending = $true },
                                                              @{ Expression = { $_.last }; Descending = $true }
    $codes = ''
    foreach ($s in $ordered) {
        $c = $Code[$s.state]
        if ($c) { $codes += $c }
    }
    return $codes
}

function Get-EffectiveState {
    if ($script:sessions.Count -eq 0) { return 'off' }
    $best = $null; $bestP = -1; $bestT = [datetime]::MinValue
    foreach ($s in $script:sessions.Values) {
        $p = $Priority[$s.state]
        if ($p -gt $bestP -or ($p -eq $bestP -and $s.last -gt $bestT)) {
            $best = $s.state; $bestP = $p; $bestT = $s.last
        }
    }
    return $best
}

# Pushes the aggregate state and subagent count to the device, sending only
# what changed. `force` re-sends everything (after the port was reopened).
function Sync-Device([bool]$force = $false) {
    if (-not $script:serial) { return }
    if ($script:sessions.Count -eq 0 -and -not $script:deviceState) {   # nothing known yet
        Send-SessionLines $force
        return
    }

    $eff = Get-EffectiveState
    if ($force -or $eff -ne $script:deviceState) {
        if (Send-Serial $eff) {
            Log "device <- $eff"
            $script:deviceState = $eff
            if ($eff -in 'idle', 'off', 'error', 'paused') {   # the firmware resets too
                $script:deviceSubagents = 0
                $script:deviceTool = $false
            }
        }
    }

    $total = 0
    foreach ($s in $script:sessions.Values) { $total += $s.subagents }
    while ($script:deviceSubagents -lt $total) {
        if (-not (Send-Serial 'subagent_start')) { break }
        $script:deviceSubagents++
        Log "device <- subagent_start ($($script:deviceSubagents))"
    }
    while ($script:deviceSubagents -gt $total) {
        if (-not (Send-Serial 'subagent_stop')) { break }
        $script:deviceSubagents--
        Log "device <- subagent_stop ($($script:deviceSubagents))"
    }

    $codes = Get-SessionCodes
    if ($force -or $codes -ne $script:deviceSessions) {
        $line = if ($codes) { "sessions $codes" } else { 'sessions' }
        if (Send-Serial $line) {
            Log "device <- $line"
            $script:deviceSessions = $codes
        }
    }

    # Blue dot: a tool process is running in the session the device is showing
    $tool = $false
    foreach ($s in $script:sessions.Values) {
        if ($s.state -eq $eff -and $s.tools.Count -gt 0) { $tool = $true; break }
    }
    if ($force -or $tool -ne $script:deviceTool) {
        $line = if ($tool) { 'tool_start' } else { 'tool_stop' }
        if (Send-Serial $line) {
            Log "device <- $line"
            $script:deviceTool = $tool
        }
    }

    Send-SessionLines $force
}

# ---------------- hook events -> the device's inspector ----------------

$script:events     = [System.Collections.Generic.List[object]]::new()   # last 50 formatted events
$script:eventsSent = 0

# One value as a single line of at most EventValueMax characters
function Format-EventValue($v) {
    if ($null -eq $v) { return '' }
    $s = if ($v -is [string]) { $v }
         elseif ($v -is [bool] -or $v -is [ValueType]) { [string]$v }
         else { $v | ConvertTo-Json -Compress -Depth 6 }
    $s = $s -replace '[\t\r\n]+', ' '
    $s = $s -replace '[\x00-\x1F]', ''
    if ($s.Length -gt $EventValueMax) { $s = $s.Substring(0, $EventValueMax - 3) + '...' }
    return $s
}

# "event <name>\tsummary=..\tproject=..\tproject_root=..\t<key>=<value>..":
# the daemon's own fields first, then every top-level field of the hook
# payload, objects flattened one level ("tool_input.command"), in the order
# Claude Code sent them
function Format-EventLine($e, [string]$tag, [string]$project, [string]$root) {
    $parts = [System.Collections.Generic.List[string]]::new()
    $parts.Add("summary=$tag")
    if ($project) { $parts.Add("project=$(Format-EventValue $project)") }
    if ($root)    { $parts.Add("project_root=$(Format-EventValue $root)") }
    foreach ($p in $e.PSObject.Properties) {
        $v = $p.Value
        if ($v -is [System.Management.Automation.PSCustomObject]) {
            foreach ($q in $v.PSObject.Properties) {
                $parts.Add("$($p.Name).$($q.Name)=$(Format-EventValue $q.Value)")
            }
        } else {
            $parts.Add("$($p.Name)=$(Format-EventValue $v)")
        }
    }
    $line = "event $([string]$e.hook_event_name)`t" + ($parts -join "`t")
    if ($line.Length -gt 3800) { $line = $line.Substring(0, 3800) }
    return $line
}

function Add-Event([string]$tag, [string]$line) {
    $script:events.Add([ordered]@{ time = (Get-Date).ToString('HH:mm:ss.fff'); tag = $tag; line = $line })
    while ($script:events.Count -gt 50) { $script:events.RemoveAt(0) }
}

# Only a board that asked for them gets event lines: they are long, and a
# board without the feature would drop them anyway
function Send-Event([string]$line) {
    if (-not $script:serial -or -not $script:deviceInfo) { return }
    if (([string]$script:deviceInfo.features) -notmatch '(^|,)events(,|$)') { return }
    if (Send-Serial $line) { $script:eventsSent++ }
}

function Write-EventLog($e) {
    try {
        if ((Test-Path $EventLogPath) -and (Get-Item $EventLogPath).Length -gt 20MB) {
            Move-Item -Force $EventLogPath "$EventLogPath.1"
        }
        $obj = [ordered]@{ received = (Get-Date).ToString('o') }
        foreach ($p in $e.PSObject.Properties) { $obj[$p.Name] = $p.Value }
        Add-Content -Path $EventLogPath -Value ($obj | ConvertTo-Json -Compress -Depth 10) -Encoding utf8
    } catch {
        Log "event log: $($_.Exception.Message)"
    }
}

# ---------------- hook events -> states ----------------

function Map-Event($e) {
    switch ([string]$e.hook_event_name) {
        'SessionStart'     { return 'idle' }
        'UserPromptSubmit' { return 'processing' }
        'PreToolUse'       { if ([string]$e.tool_name -eq 'AskUserQuestion') { return 'question' }; return 'processing' }
        'PostToolUse'      { return 'processing' }
        'PostToolUseFailure' { return 'processing' }   # a failed tool: Claude carries on
        'PostToolBatch'    { return 'processing' }   # a batch of parallel tools resolved, next model call
        'Notification' {
            switch -Regex ([string]$e.notification_type) {
                '^(permission_prompt|quota_auto_resume_stale)$'                   { return 'waiting_user' }
                '^(elicitation_dialog|elicitation_url_dialog|agent_needs_input)$' { return 'question' }
                '^quota_auto_resume_fired$'                                       { return 'processing' }
                '^quota_auto_resume_disabled$'                                    { return 'error' }
            }
            return $null
        }
        'Stop'             { return 'idle' }
        'StopFailure'      { if ([string]$e.error_type -eq 'rate_limit') { return 'paused' }; return 'error' }
        'PreCompact'       { return 'compacting' }
        'PostCompact' {
            $t = if ($e.trigger) { $e.trigger } else { $e.compact_reason }
            if ([string]$t -eq 'manual') { return 'idle' }
            return 'processing'
        }
        'SubagentStart'    { return 'subagent_start' }
        'SubagentStop'     { return 'subagent_stop' }
        'SessionEnd'       { return 'off' }
    }
    return $null
}

function Process-Hook($e, [string]$rootHeader, [int]$clientPid) {
    $sid   = if ($e.session_id) { [string]$e.session_id } else { 'unknown' }
    $short = $sid.Substring(0, [Math]::Min(8, $sid.Length))
    $detail = @($e.notification_type, $e.error_type, $e.tool_name, $e.trigger, $e.compact_reason) | Where-Object { $_ } | Select-Object -First 1
    $tag = if ($detail) { "$($e.hook_event_name)/$detail" } else { [string]$e.hook_event_name }

    # The project: the header (the session's root) wins; else what the
    # session already had; else where its transcript starts; else the cwd
    # of this first event (a session first seen mid-way, after a daemon
    # restart, may have cd'd into a subfolder)
    $known = if ($script:sessions.ContainsKey($sid)) { $script:sessions[$sid] } else { $null }
    $rootFrom = 'header'
    $root = if ($rootHeader) { $rootHeader }
            elseif ($known -and $known.root) { $rootFrom = 'known'; $known.root }
            else { $null }
    if (-not $root) {
        $rootFrom = 'transcript'
        $root = Get-TranscriptRoot ([string]$e.transcript_path)
        if (-not $root -and $e.cwd) { $rootFrom = 'cwd'; $root = [string]$e.cwd }
    }
    $project = Get-ProjectName $root

    # Every event goes to the inspector, the ring and the JSON log, mapped or
    # not; the device gets it after its session line (Sync-Device below)
    $eventLine = Format-EventLine $e $tag $project $root
    Add-Event $tag $eventLine
    if ($LogEvents) { Write-EventLog $e }

    $cmd = Map-Event $e
    if (-not $cmd) {
        Log "hook $tag session=$short project=$project : ignored"
        Send-Event $eventLine
        return
    }
    $eventSent = $false
    if ($e.hook_event_name -eq 'SessionEnd') {
        Send-Event $eventLine   # the tile sees its last event before session_end takes it down
        $eventSent = $true
        $script:sessions.Remove($sid)
    } else {
        if (-not $known) { Log "session ${short}: root $root (from $rootFrom)" }
        $s = Get-Session $sid
        switch ($cmd) {
            'subagent_start' { $s.subagents++ }
            'subagent_stop'  {
                if ($s.subagents -gt 0) {
                    $s.subagents--
                } elseif ($s.state -eq 'compacting') {
                    # The compaction runs as a subagent that stops without having
                    # started: its stop is the end of the compaction. On a session
                    # left idle (Claude Code compacts it 54 min after Stop, before
                    # the prompt cache expires) PostCompact only comes with the
                    # next prompt, hours later, so this is the only end signal
                    $s.state = $s.beforeCompact
                }
            }
            default {
                if ($cmd -eq 'compacting' -and $s.state -ne 'compacting') { $s.beforeCompact = $s.state }
                $s.state = $cmd
            }
        }
        # Any activity from a session that was waiting means the permission went through
        if ($cmd -in 'subagent_start', 'subagent_stop' -and $s.state -eq 'waiting_user') { $s.state = 'processing' }
        $s.last = Get-Date
        if ($clientPid -gt 0 -and $clientPid -ne $s.pid) {
            $s.pid    = $clientPid
            $s.dead   = $false
            $s.tools  = @{}
            $s.seeded = $false   # the next snapshot records what already runs under it
            Log "session ${short}: Claude Code process pid $clientPid"
        }
        if ($e.hook_event_name -in 'PostToolUse', 'PostToolUseFailure' -and $s.tools.Count -gt 0) {
            # The tool is over: forget its processes without waiting for the next snapshot
            foreach ($t in @($s.tools.Keys)) { if (-not (Test-ProcessAlive $t)) { $s.tools.Remove($t) } }
        }
        if ($e.transcript_path) { $s.transcript = [string]$e.transcript_path }
        if ($root -and ($rootHeader -or -not $s.root)) {
            $s.root    = $root
            $s.project = $project
        }
    }
    Log "hook $tag session=$short project=$project -> $cmd (sessions=$($script:sessions.Count))"
    Expire-Sessions
    Sync-Device
    if (-not $eventSent) { Send-Event $eventLine }
}

# ---------------- account usage -> the device's screensaver ----------------

$script:usage          = [ordered]@{}            # key -> @{ label; percent; resetsAt (DateTimeOffset or $null); severity }
$script:usageSplit     = @()                     # the week by app: @{ label; percent }, from the account
$script:usageSource    = $null                   # 'statusline' or 'account'
$script:usageAt        = [datetime]::MinValue    # when the numbers were read
$script:usageNote      = $(if ($AccountUsage) { 'not polled yet' } else { 'account polling off (no -AccountUsage)' })
$script:usageNextPoll  = (Get-Date).AddSeconds(5)
$script:usageTask      = $null                   # the poll in flight
$script:usageHttp      = $null
$script:usageCheckAt   = [datetime]::MinValue
$script:deviceUsageKey = $null                   # what the device last got
$script:deviceUsageAt  = [datetime]::MinValue
$script:usagePlan      = $null                   # @{ name; status; since (DateTimeOffset or $null) }
$script:usageExtra     = $null                   # @{ state = on|off|full; used; limit; currency }
$script:profileTask    = $null                   # the profile request in flight
$script:profileNextPoll = [datetime]::MinValue
$script:usageHistory   = [System.Collections.Generic.List[object]]::new()   # the weekly limit's readings: @{ t; p; r }
$script:usageProjects  = @()                     # @{ name; week; today; tokens }, week and today in % of the weekly limit

function Set-UsageNote([string]$note) {
    if ($note -ne $script:usageNote) { Log "usage: $note" }
    $script:usageNote = $note
}

function Get-UsageLabel([string]$kind, [string]$model) {
    switch ($kind) {
        'session'       { return 'Current session' }
        'weekly_all'    { return 'This week' }
        'weekly_scoped' { if ($model) { return "This week ($model)" } else { return 'This week (scoped)' } }
        default         { return ($kind -replace '_', ' ') }
    }
}

# Each limit's window, for the pace on the usage page
function Get-UsageWindow([string]$kind) {
    switch ($kind) {
        'session' { return 5 * 3600 }
        { $_ -like 'weekly*' } { return 7 * 24 * 3600 }
        default   { return 0 }
    }
}

# "Max 5x", "Pro": the login's subscriptionType / rateLimitTier, or the
# profile's organization_type / rate_limit_tier
function Get-PlanName([string]$type, [string]$tier) {
    if ($tier -match 'max_(\d+x)') { return "Max $($Matches[1])" }
    switch -Regex ($type) {
        'max'        { return 'Max' }
        'pro'        { return 'Pro' }
        'team'       { return 'Team' }
        'enterprise' { return 'Enterprise' }
    }
    if ($type) { return (Get-Culture).TextInfo.ToTitleCase((($type -replace '^claude_', '') -replace '_', ' ')) }
    return $null
}

# A reset time as the sources give it: ISO 8601 (ConvertFrom-Json may have
# made it a DateTime already) or Unix seconds (the status line)
function ConvertTo-ResetTime($v) {
    if ($null -eq $v -or "$v" -eq '') { return $null }
    if ($v -is [datetime]) { return [DateTimeOffset]$v }
    if ($v -is [ValueType]) { return [DateTimeOffset]::FromUnixTimeSeconds([int64]$v) }
    try { return [DateTimeOffset]::Parse([string]$v, [cultureinfo]::InvariantCulture) } catch { return $null }
}

# The endpoint's answer: its "limits" list (session, weekly_all,
# weekly_scoped per model), else the older five_hour / seven_day objects
function Set-UsageFromAccount($j) {
    $next = [ordered]@{}
    if ($j.limits) {
        foreach ($l in $j.limits) {
            if ($null -eq $l.percent) { continue }
            $model = [string]$l.scope.model.display_name
            $key = if ($model) { "$($l.kind):$model" } else { [string]$l.kind }
            $next[$key] = @{ label = (Get-UsageLabel $l.kind $model); percent = [double]$l.percent
                             resetsAt = (ConvertTo-ResetTime $l.resets_at); severity = [string]$l.severity
                             window = (Get-UsageWindow $l.kind) }
        }
    } else {
        foreach ($p in @(@('five_hour', 'session'), @('seven_day', 'weekly_all'))) {
            $w = $j.($p[0])
            if ($w -and $null -ne $w.utilization) {
                $next[$p[1]] = @{ label = (Get-UsageLabel $p[1] ''); percent = [double]$w.utilization
                                  resetsAt = (ConvertTo-ResetTime $w.resets_at); severity = ''; window = (Get-UsageWindow $p[1]) }
            }
        }
    }
    if ($next.Count -eq 0) { return $false }
    $script:usage = $next
    # How the week divides between the apps (Claude Code, chats, Cowork)
    $script:usageSplit = @(foreach ($r in @($j.seven_day_breakdown.rows)) {
        if ($r -and $null -ne $r.percent -and [double]$r.percent -gt 0) {
            @{ key = [string]$r.key; label = [string]$(if ($r.display_name) { $r.display_name } else { $r.key }); percent = [double]$r.percent }
        }
    })
    $script:usageExtra = Get-ExtraUsage $j
    $script:usageSource = 'account'
    $script:usageAt = Get-Date
    Add-UsageReading
    return $true
}

# The extra usage the plan can buy past its limits, from "spend" (amounts
# in minor units with their exponent), else the older "extra_usage". Only
# the disabled form has been seen so far; the enabled one is read by the
# same field names
function Get-ExtraUsage($j) {
    $sp = $j.spend
    $eu = $j.extra_usage
    if (-not $sp -and -not $eu) { return $null }
    $enabled = if ($sp -and $null -ne $sp.enabled) { [bool]$sp.enabled } else { [bool]$eu.is_enabled }
    if (-not $enabled) { return @{ state = 'off' } }
    $inv = [cultureinfo]::InvariantCulture
    $currency = 'USD'
    $used = $null
    $limit = $null
    if ($sp -and $sp.used -and $null -ne $sp.used.amount_minor) {
        if ($sp.used.currency) { $currency = [string]$sp.used.currency }
        $used = [double]$sp.used.amount_minor / [Math]::Pow(10, [int]$sp.used.exponent)
        if ($sp.limit -and $null -ne $sp.limit.amount_minor) { $limit = [double]$sp.limit.amount_minor / [Math]::Pow(10, [int]$sp.limit.exponent) }
    } elseif ($eu) {
        $dp = if ($null -ne $eu.decimal_places) { [int]$eu.decimal_places } else { 2 }
        if ($eu.currency) { $currency = [string]$eu.currency }
        if ($null -ne $eu.used_credits) { $used = [double]$eu.used_credits / [Math]::Pow(10, $dp) }
        if ($null -ne $eu.monthly_limit) { $limit = [double]$eu.monthly_limit / [Math]::Pow(10, $dp) }
    }
    $full = ($sp -and $sp.severity -eq 'critical') -or ($eu -and [bool]$eu.spend_limit_reached)
    return @{ state = $(if ($full) { 'full' } else { 'on' }); currency = $currency
              used  = $(if ($null -ne $used) { $used.ToString('0.00', $inv) } else { '0.00' })
              limit = $(if ($null -ne $limit) { $limit.ToString('0.00', $inv) } else { '-' }) }
}

# The status line's rate_limits: the session and the week only, so the
# per-model rows the account poll found stay as they are
function Set-UsageFromStatusLine($rl) {
    $any = $false
    foreach ($p in @(@('five_hour', 'session'), @('seven_day', 'weekly_all'))) {
        $w = $rl.($p[0])
        if (-not $w -or $null -eq $w.used_percentage) { continue }
        $script:usage[$p[1]] = @{ label = (Get-UsageLabel $p[1] ''); percent = [double]$w.used_percentage
                                  resetsAt = (ConvertTo-ResetTime $w.resets_at); severity = ''; window = (Get-UsageWindow $p[1]) }
        $any = $true
    }
    if ($any) {
        $script:usageSource = 'statusline'
        $script:usageAt = Get-Date
        Add-UsageReading
    }
    return $any
}

# ---------------- the week's readings, for the chart on the usage page ----------------

# One JSON line per reading of the weekly limit in UsageHistoryPath: t (when
# read), p (percent), r (that window's reset); 35 days are kept
function Initialize-UsageHistory {
    $cut = [DateTimeOffset]::Now.AddDays(-35)
    if (-not (Test-Path -LiteralPath $UsageHistoryPath)) {
        Import-UsageHistoryFromLog
        return
    }
    $dropped = 0
    foreach ($line in [IO.File]::ReadLines($UsageHistoryPath)) {
        try { $o = $line | ConvertFrom-Json } catch { $dropped++; continue }
        $t = ConvertTo-ResetTime $o.t
        if (-not $t -or $t -lt $cut) { $dropped++; continue }
        $script:usageHistory.Add(@{ t = $t; p = [double]$o.p; r = (ConvertTo-ResetTime $o.r) })
    }
    if ($dropped) { Save-UsageHistory }
    Log "usage: $($script:usageHistory.Count) readings of the week in $UsageHistoryPath"
}

function ConvertTo-HistoryLine($e) {
    return ([ordered]@{ t = $e.t.ToString('o'); p = $e.p; r = $(if ($e.r) { $e.r.ToString('o') } else { $null }) } | ConvertTo-Json -Compress)
}

function Save-UsageHistory {
    try {
        [IO.File]::WriteAllLines($UsageHistoryPath, [string[]]@($script:usageHistory | ForEach-Object { ConvertTo-HistoryLine $_ }))
    } catch { Log "usage: cannot write ${UsageHistoryPath}: $($_.Exception.Message)" }
}

# The first time: the usage lines the log recorded (each change is logged
# as "device <- usage ... limit=This week|<percent>|<resets in s>|...")
function Import-UsageHistoryFromLog {
    $inv = [cultureinfo]::InvariantCulture
    foreach ($f in @("$LogPath.1", $LogPath)) {
        if (-not (Test-Path -LiteralPath $f)) { continue }
        foreach ($m in (Select-String -LiteralPath $f -Pattern '^(\S+ \S+) device <- usage \S+ age=(\d+).*? limit=This week\|([\d.]+)\|(-?\d+)\|')) {
            $g = $m.Matches[0].Groups
            try { $at = [DateTimeOffset]::new([datetime]::ParseExact($g[1].Value, 'yyyy-MM-dd HH:mm:ss.fff', $inv)) } catch { continue }
            $reset = [int]$g[4].Value
            $script:usageHistory.Add(@{ t = $at.AddSeconds(-[int]$g[2].Value); p = [double]::Parse($g[3].Value, $inv)
                                        r = $(if ($reset -ge 0) { $at.AddSeconds($reset) } else { $null }) })
        }
    }
    Save-UsageHistory
    Log "usage: $($script:usageHistory.Count) readings of the week taken from the log into $UsageHistoryPath"
}

# Keeps the weekly limit's reading: when it changed, or an hour after the last one
function Add-UsageReading {
    $w = $script:usage['weekly_all']
    if (-not $w -or -not $w.resetsAt) { return }
    $now = [DateTimeOffset]::Now
    $n = $script:usageHistory.Count
    $last = if ($n) { $script:usageHistory[$n - 1] } else { $null }
    if ($last -and $last.p -eq $w.percent -and ($now - $last.t).TotalMinutes -lt 60) { return }
    $e = @{ t = $now; p = [double]$w.percent; r = $w.resetsAt }
    $script:usageHistory.Add($e)
    while ($script:usageHistory.Count -and $script:usageHistory[0].t -lt $now.AddDays(-35)) { $script:usageHistory.RemoveAt(0) }
    try { Add-Content -LiteralPath $UsageHistoryPath -Value (ConvertTo-HistoryLine $e) -Encoding utf8 } catch {}
}

# The weekly limit's percentage at every hour of its window so far: the
# last reading of that window before each hour, if it is at most three
# hours older ("" otherwise; the board draws a straight line over gaps).
# "<start>|3600|0,4,4,,9,..."
function Format-UsageHistory {
    $w = $script:usage['weekly_all']
    if (-not $w -or -not $w.resetsAt) { return $null }
    $start = $w.resetsAt.AddDays(-7)
    $now = [DateTimeOffset]::Now
    $mine = @($script:usageHistory | Where-Object { $_.r -and [Math]::Abs(($_.r - $w.resetsAt).TotalMinutes) -lt 90 -and $_.t -ge $start } |
              Sort-Object { $_.t })
    $values = [System.Collections.Generic.List[string]]::new()
    $values.Add('0')
    $i = 0
    $last = $null
    for ($k = 1; $start.AddHours($k) -le $now -and $k -lt 200; $k++) {
        $tk = $start.AddHours($k)
        while ($i -lt $mine.Count -and $mine[$i].t -le $tk) { $last = $mine[$i]; $i++ }
        if ($last -and ($tk - $last.t).TotalHours -le 3) { $values.Add([string][int][Math]::Round($last.p)) } else { $values.Add('') }
    }
    return '{0}|3600|{1}' -f $start.ToUnixTimeSeconds(), ($values -join ',')
}

# "usage <source>\tage=<s>\tlimit=<label>|<percent>|<resets in s>|<severity>..."
function Format-UsageLine {
    if ($script:usage.Count -eq 0) { return 'usage none' }
    $inv = [cultureinfo]::InvariantCulture
    $now = [DateTimeOffset]::Now
    $age = [int][Math]::Max(0, ((Get-Date) - $script:usageAt).TotalSeconds)
    $parts = @("usage $($script:usageSource)", "age=$age")
    foreach ($e in $script:usage.Values) {
        $reset = -1
        if ($e.resetsAt) { $reset = [int][Math]::Max(0, ($e.resetsAt - $now).TotalSeconds) }
        $label = $e.label -replace '[\t|]', ' '
        if ($label.Length -gt 28) { $label = $label.Substring(0, 28) }
        $parts += 'limit=' + [string]::Format($inv, '{0}|{1:0.#}|{2}|{3}', $label, $e.percent, $reset, $e.severity)
    }
    if ($script:usageSplit.Count) {
        $parts += 'split=' + (@($script:usageSplit | ForEach-Object {
            [string]::Format($inv, '{0}:{1:0.#}', ($_.label -replace '[\t|:]', ' '), $_.percent)
        }) -join '|')
    }
    # For the usage page (protocol 8; older boards skip these fields)
    $parts += 'now={0}|{1}' -f $now.ToUnixTimeSeconds(), [int]$now.Offset.TotalSeconds
    $parts += 'windows=' + (@($script:usage.Values | ForEach-Object { [int]$_.window }) -join '|')
    $pl = $script:usagePlan
    if ($pl -and $pl.name) {
        $since = if ($pl.since) { $pl.since.ToUnixTimeSeconds() } else { '' }
        $parts += 'plan={0}|{1}|{2}' -f ($pl.name -replace '[\t|]', ' '), $pl.status, $since
    }
    $x = $script:usageExtra
    if ($x) {
        $parts += $(if ($x.state -eq 'off') { 'extra=off' } else { 'extra={0}|{1}|{2}|{3}' -f $x.state, $x.used, $x.limit, $x.currency })
    }
    $h = Format-UsageHistory
    if ($h) { $parts += "history=$h" }
    foreach ($pr in $script:usageProjects) {
        $parts += 'project=' + [string]::Format($inv, '{0}|{1:0.#}|{2:0.#}', ($pr.name -replace '[\t|]', ' '), $pr.week, $pr.today)
    }
    return ($parts -join "`t")
}

# Sends the usage line when the numbers changed, when new ones arrived and
# the device's copy is five minutes old (its "updated ... ago"), or forced.
# A window whose reset time has passed is dropped, as Claude Code does.
function Send-Usage([bool]$force = $false) {
    if (-not $script:serial) { return }
    if (([string]$script:deviceInfo.features) -notmatch '(^|,)usage(,|$)') { return }
    $now = [DateTimeOffset]::Now
    foreach ($k in @($script:usage.Keys)) {
        $r = $script:usage[$k].resetsAt
        if ($r -and $r -lt $now) { $script:usage.Remove($k) }
    }
    $inv = [cultureinfo]::InvariantCulture
    $key = "$($script:usageSource)|" + (@($script:usage.Values | ForEach-Object {
        $min = if ($_.resetsAt) { [int]($_.resetsAt.ToUnixTimeSeconds() / 60) } else { '-' }
        [string]::Format($inv, '{0}={1:0.#}@{2}', $_.label, $_.percent, $min)
    }) -join ';') + '|' + (@($script:usageSplit | ForEach-Object { "$($_.label)=$($_.percent)" }) -join ';') +
        '|' + $(if ($script:usagePlan) { "$($script:usagePlan.name) $($script:usagePlan.status)" }) +
        '|' + $(if ($script:usageExtra) { "$($script:usageExtra.state) $($script:usageExtra.used) $($script:usageExtra.limit)" }) +
        '|' + (Format-UsageHistory) +
        '|' + (@($script:usageProjects | ForEach-Object { [string]::Format($inv, '{0}={1:0}/{2:0}', $_.name, $_.week, $_.today) }) -join ';')
    $changed = $key -ne $script:deviceUsageKey
    $refresh = $script:usageAt -gt $script:deviceUsageAt -and ((Get-Date) - $script:deviceUsageAt).TotalSeconds -ge 300
    if (-not ($force -or $changed -or $refresh)) { return }
    $line = Format-UsageLine
    if (Send-Serial $line) {
        if ($force -or $changed) { Log "device <- $(($line -replace 'history=[^\t]*', 'history=...') -replace "`t", ' ')" }
        $script:deviceUsageKey = $key
        $script:deviceUsageAt = Get-Date
    }
}

# Claude Code's claude.ai login from CredentialsPath, or $null (and a note why)
function Get-AccountLogin {
    try {
        $o = (Get-Content -LiteralPath $CredentialsPath -Raw -ErrorAction Stop | ConvertFrom-Json).claudeAiOauth
    } catch {
        Set-UsageNote "no Claude Code login in $CredentialsPath"
        return $null
    }
    if (-not $o -or -not $o.accessToken) { Set-UsageNote 'no claude.ai login in the credentials file'; return $null }
    if ($o.expiresAt -and [DateTimeOffset]::FromUnixTimeMilliseconds([int64]$o.expiresAt) -lt [DateTimeOffset]::Now.AddMinutes(1)) {
        Set-UsageNote 'access token expired: waiting for Claude Code to refresh it'
        $script:usageNextPoll = (Get-Date).AddMinutes(2)
        return $null
    }
    return $o
}

# A GET of an account endpoint Claude Code reads, with its login; returns the task
function Send-AccountRequest([string]$path, $login) {
    if (-not $script:usageHttp) {
        $script:usageHttp = [System.Net.Http.HttpClient]::new()
        $script:usageHttp.Timeout = [TimeSpan]::FromSeconds(15)
    }
    $req = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::Get, "https://api.anthropic.com$path")
    $req.Headers.Authorization = [System.Net.Http.Headers.AuthenticationHeaderValue]::new('Bearer', [string]$login.accessToken)
    $req.Headers.Add('anthropic-beta', 'oauth-2025-04-20')
    $req.Headers.UserAgent.ParseAdd('claude-monitor')
    return $script:usageHttp.SendAsync($req)
}

# Starts one poll of the endpoint /usage reads, without waiting for it: the
# main loop picks the answer up (Complete-UsagePoll). Every six hours also
# the account's profile, for the plan's status and start (Complete-ProfilePoll);
# until it answers the plan's name comes from the login itself
function Start-UsagePoll {
    $script:usageNextPoll = (Get-Date).AddSeconds($UsagePollSeconds)
    $o = Get-AccountLogin
    if (-not $o) { return }
    if (-not $script:usagePlan -or -not $script:usagePlan.status) {
        $name = Get-PlanName ([string]$o.subscriptionType) ([string]$o.rateLimitTier)
        if ($name) { $script:usagePlan = @{ name = $name; status = ''; since = $null } }
    }
    try {
        $script:usageTask = Send-AccountRequest '/api/oauth/usage' $o
        if (-not $script:profileTask -and (Get-Date) -ge $script:profileNextPoll) {
            $script:profileNextPoll = (Get-Date).AddHours(6)
            $script:profileTask = Send-AccountRequest '/api/oauth/profile' $o
        }
    } catch {
        Set-UsageNote "poll failed: $($_.Exception.Message)"
    }
}

function Complete-ProfilePoll {
    $t = $script:profileTask
    $script:profileTask = $null
    if ($t.IsFaulted -or $t.IsCanceled) { $script:profileNextPoll = (Get-Date).AddMinutes(30); return }
    $resp = $t.Result
    try {
        $code = [int]$resp.StatusCode
        if ($code -ne 200) {
            Log "usage: profile: HTTP $code"
            $script:profileNextPoll = (Get-Date).AddMinutes($(if ($code -eq 429) { 60 } else { 30 }))
            return
        }
        $org = ($resp.Content.ReadAsStringAsync().GetAwaiter().GetResult() | ConvertFrom-Json).organization
        $name = Get-PlanName ([string]$org.organization_type) ([string]$org.rate_limit_tier)
        if ($name) {
            $script:usagePlan = @{ name = $name; status = [string]$org.subscription_status; since = (ConvertTo-ResetTime $org.subscription_created_at) }
        }
    } catch {
        Log "usage: profile: $($_.Exception.Message)"
    } finally {
        $resp.Dispose()
    }
}

function Complete-UsagePoll {
    $t = $script:usageTask
    $script:usageTask = $null
    if ($t.IsFaulted -or $t.IsCanceled) {
        $msg = if ($t.Exception) { $t.Exception.GetBaseException().Message } else { 'timed out' }
        Set-UsageNote "poll failed: $msg"
        return
    }
    $resp = $t.Result
    try {
        $code = [int]$resp.StatusCode
        if ($code -eq 429) {
            $script:usageNextPoll = (Get-Date).AddMinutes(15)
            Set-UsageNote 'rate limited (HTTP 429): next poll in 15 min'
            return
        }
        if ($code -ne 200) { Set-UsageNote "poll failed: HTTP $code"; return }
        $j = $resp.Content.ReadAsStringAsync().GetAwaiter().GetResult() | ConvertFrom-Json
        if (Set-UsageFromAccount $j) {
            Set-UsageNote 'ok'
            if (-not $script:transcriptTask) { Update-ProjectUsage }
            Send-Usage
        } else {
            Set-UsageNote 'no limits in the answer'
        }
    } catch {
        Set-UsageNote "poll failed: $($_.Exception.Message)"
    } finally {
        $resp.Dispose()
    }
}

function Get-UsageObject {
    [ordered]@{
        source          = $script:usageSource
        read_at         = $(if ($script:usageAt -gt [datetime]::MinValue) { $script:usageAt.ToString('s') } else { $null })
        account_polling = [bool]$AccountUsage
        note            = $script:usageNote
        limits          = @($script:usage.GetEnumerator() | ForEach-Object {
            [ordered]@{ key = $_.Key; label = $_.Value.label; percent = $_.Value.percent
                        resets_at = $(if ($_.Value.resetsAt) { $_.Value.resetsAt.ToString('o') } else { $null }); severity = $_.Value.severity }
        })
        week_by_app     = @($script:usageSplit | ForEach-Object { [ordered]@{ label = $_.label; percent = $_.percent } })
        plan            = $(if ($script:usagePlan) { [ordered]@{ name = $script:usagePlan.name; status = $script:usagePlan.status
                                                                  since = $(if ($script:usagePlan.since) { $script:usagePlan.since.ToString('yyyy-MM-dd') } else { $null }) } })
        extra_usage     = $script:usageExtra
        week_readings   = $script:usageHistory.Count
        week_by_hour    = (Format-UsageHistory)
        projects        = @($script:usageProjects | ForEach-Object { [ordered]@{ name = $_.name; week = [Math]::Round($_.week, 1); today = [Math]::Round($_.today, 1); tokens = $_.tokens } })
        transcripts     = $script:transcriptNote
    }
}

# ---------------- Claude Code's transcripts -> usage by project ----------------
#
# The account splits the week by app (Claude Code, chats, Cowork), not by
# project. Claude Code's transcripts have every message's token counts, so
# the daemon weighs them per project (where the session started) and splits
# Claude Code's part of the weekly limit in the same proportions; today's
# part is the same weight since local midnight. An estimate: the limits are
# not published in tokens.

$script:transcriptOn     = $false
$script:transcriptNote   = $(if ($NoProjectUsage) { 'off (-NoProjectUsage)' } else { 'not read yet' })
$script:transcriptTask   = $null
$script:transcriptNextAt = [datetime]::MinValue

$TranscriptUsageSource = @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

// Claude Code's token use per project and hour, from its own transcripts:
// <root>\<project dir>\<session>.jsonl (subagents in <session>\subagents\),
// one JSON object per line, every assistant message with message.usage.
// Read incrementally: a scan parses only what was appended since the last
// one. A message written as several lines (one per content block) is
// counted once, by message.id, at its largest usage.
//
// Tokens are weighted the way the API prices them relative to fresh input
// (cache writes 1.25x for 5 min and 2x for 1 h, cache reads 0.1x, output
// 5x; Sonnet 0.6x and Haiku 0.2x of Opus), so a project's share reads like
// its share of the cost, which is what the plan limits follow; an estimate.
public static class TranscriptUsage {
    public class Row { public string Root; public string Folder; public double Week; public double Today; public long Tokens; }

    class FileState { public long Offset; }
    class Counted { public double Weight; public long Tokens; public long Hour; public string Folder; }
    class Bucket { public double Weight; public long Tokens; }

    static readonly object gate = new object();
    static readonly Dictionary<string, FileState> files = new Dictionary<string, FileState>(StringComparer.OrdinalIgnoreCase);
    static readonly Dictionary<string, Counted> messages = new Dictionary<string, Counted>();
    static readonly Dictionary<string, Dictionary<long, Bucket>> hours = new Dictionary<string, Dictionary<long, Bucket>>(StringComparer.OrdinalIgnoreCase);
    static readonly Dictionary<string, string> roots = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
    static readonly byte[] assistantMark = Encoding.UTF8.GetBytes("\"type\":\"assistant\"");
    static readonly byte[] cwdMark = Encoding.UTF8.GetBytes("\"cwd\":");

    public static long BytesRead;
    public static int FilesRead;
    public static double LastScanMs;
    public static string LastError;

    public static Task ScanAsync(string root, int days) { return Task.Run(() => Scan(root, days)); }

    public static void Scan(string root, int days) {
        lock (gate) {
            var sw = Stopwatch.StartNew();
            DateTime cutoff = DateTime.UtcNow.AddDays(-days);
            long minHour = new DateTimeOffset(cutoff).ToUnixTimeSeconds() / 3600;
            FilesRead = 0;
            if (Directory.Exists(root)) {
                foreach (string path in Directory.EnumerateFiles(root, "*.jsonl", SearchOption.AllDirectories)) {
                    var fi = new FileInfo(path);
                    if (fi.LastWriteTimeUtc < cutoff) continue;
                    FileState st;
                    if (!files.TryGetValue(path, out st)) { st = new FileState(); files[path] = st; }
                    if (fi.Length == st.Offset) continue;
                    if (fi.Length < st.Offset) st.Offset = 0;   // rewritten
                    string rel = Path.GetRelativePath(root, path);
                    int sep = rel.IndexOfAny(new[] { '\\', '/' });
                    if (sep <= 0) continue;
                    string folder = rel.Substring(0, sep);
                    bool main = rel.IndexOfAny(new[] { '\\', '/' }, sep + 1) < 0;   // the session's own file, not a subagent's
                    try { ReadFile(path, st, folder, main, minHour); FilesRead++; }
                    catch (Exception e) { LastError = Path.GetFileName(path) + ": " + e.Message; }
                }
            }
            Prune(minHour);
            LastScanMs = sw.Elapsed.TotalMilliseconds;
        }
    }

    static void ReadFile(string path, FileState st, string folder, bool main, long minHour) {
        using (var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 16)) {
            fs.Position = st.Offset;
            byte[] buf = new byte[1 << 22];
            int filled = 0;
            long consumed = st.Offset;
            while (true) {
                if (filled == buf.Length) Array.Resize(ref buf, buf.Length * 2);   // a line longer than the buffer
                int n = fs.Read(buf, filled, buf.Length - filled);
                if (n <= 0) break;
                BytesRead += n;
                int scanFrom = filled;
                filled += n;
                int start = 0;
                while (true) {
                    int nl = Array.IndexOf(buf, (byte)'\n', Math.Max(start, scanFrom), filled - Math.Max(start, scanFrom));
                    if (nl < 0) break;
                    Line(buf, start, nl - start, folder, main, minHour);
                    start = nl + 1;
                }
                consumed += start;
                Buffer.BlockCopy(buf, start, buf, 0, filled - start);
                filled -= start;
            }
            st.Offset = consumed;   // a last line without its newline is read next time
        }
    }

    static void Line(byte[] buf, int start, int len, string folder, bool main, long minHour) {
        if (len < 2) return;
        var span = new ReadOnlySpan<byte>(buf, start, len);
        bool isAssistant = span.IndexOf(assistantMark) >= 0;
        bool wantRoot = main && !roots.ContainsKey(folder) && span.IndexOf(cwdMark) >= 0;
        if (!isAssistant && !wantRoot) return;
        JsonDocument doc;
        try { doc = JsonDocument.Parse(new ReadOnlyMemory<byte>(buf, start, len)); } catch { return; }
        using (doc) {
            JsonElement o = doc.RootElement;
            if (o.ValueKind != JsonValueKind.Object) return;
            JsonElement v;
            if (wantRoot && o.TryGetProperty("cwd", out v) && v.ValueKind == JsonValueKind.String) {
                roots[folder] = v.GetString();   // where the session was started: the project
            }
            if (!isAssistant) return;
            if (!o.TryGetProperty("type", out v) || v.ValueKind != JsonValueKind.String || v.GetString() != "assistant") return;
            JsonElement msg, usage;
            if (!o.TryGetProperty("message", out msg) || msg.ValueKind != JsonValueKind.Object) return;
            if (!msg.TryGetProperty("usage", out usage) || usage.ValueKind != JsonValueKind.Object) return;
            if (!o.TryGetProperty("timestamp", out v) || v.ValueKind != JsonValueKind.String) return;
            DateTimeOffset ts;
            if (!DateTimeOffset.TryParse(v.GetString(), out ts)) return;
            long hour = ts.ToUnixTimeSeconds() / 3600;
            if (hour < minHour) return;
            string model = msg.TryGetProperty("model", out v) && v.ValueKind == JsonValueKind.String ? v.GetString() : "";
            if (model == "<synthetic>") return;
            string id = msg.TryGetProperty("id", out v) && v.ValueKind == JsonValueKind.String ? v.GetString() : null;

            double input = Num(usage, "input_tokens"), output = Num(usage, "output_tokens");
            double read = Num(usage, "cache_read_input_tokens"), write = Num(usage, "cache_creation_input_tokens");
            double w5 = write, w1 = 0;
            JsonElement cc;
            if (usage.TryGetProperty("cache_creation", out cc) && cc.ValueKind == JsonValueKind.Object) {
                w5 = Num(cc, "ephemeral_5m_input_tokens");
                w1 = Num(cc, "ephemeral_1h_input_tokens");
                if (w5 + w1 == 0) w5 = write;
            }
            double weight = (input + 1.25 * w5 + 2.0 * w1 + 0.1 * read + 5.0 * output) * ModelFactor(model);
            long tokens = (long)(input + output + read + write);

            Counted c;
            if (id != null && messages.TryGetValue(id, out c)) {
                if (weight <= c.Weight) return;
                Add(c.Folder, c.Hour, weight - c.Weight, tokens - c.Tokens);
                c.Weight = weight;
                c.Tokens = tokens;
                return;
            }
            if (id != null) messages[id] = new Counted { Weight = weight, Tokens = tokens, Hour = hour, Folder = folder };
            Add(folder, hour, weight, tokens);
        }
    }

    static double Num(JsonElement o, string name) {
        JsonElement v;
        return o.TryGetProperty(name, out v) && v.ValueKind == JsonValueKind.Number ? v.GetDouble() : 0;
    }

    static double ModelFactor(string model) {
        if (model.IndexOf("haiku", StringComparison.OrdinalIgnoreCase) >= 0) return 0.2;
        if (model.IndexOf("sonnet", StringComparison.OrdinalIgnoreCase) >= 0) return 0.6;
        return 1.0;
    }

    static void Add(string folder, long hour, double weight, long tokens) {
        Dictionary<long, Bucket> h;
        if (!hours.TryGetValue(folder, out h)) { h = new Dictionary<long, Bucket>(); hours[folder] = h; }
        Bucket b;
        if (!h.TryGetValue(hour, out b)) { b = new Bucket(); h[hour] = b; }
        b.Weight += weight;
        b.Tokens += tokens;
    }

    static void Prune(long minHour) {
        var old = new List<string>();
        foreach (var kv in messages) if (kv.Value.Hour < minHour) old.Add(kv.Key);
        foreach (string k in old) messages.Remove(k);
        foreach (var h in hours.Values) {
            var gone = new List<long>();
            foreach (long k in h.Keys) if (k < minHour) gone.Add(k);
            foreach (long k in gone) h.Remove(k);
        }
    }

    // Each project's weighted use since `since` and since `today` (both UTC)
    public static Row[] Totals(DateTime since, DateTime today) {
        lock (gate) {
            long sinceHour = new DateTimeOffset(since.ToUniversalTime()).ToUnixTimeSeconds() / 3600;
            long todayHour = new DateTimeOffset(today.ToUniversalTime()).ToUnixTimeSeconds() / 3600;
            var rows = new List<Row>();
            foreach (var kv in hours) {
                var r = new Row { Folder = kv.Key };
                string root;
                r.Root = roots.TryGetValue(kv.Key, out root) ? root : null;
                foreach (var b in kv.Value) {
                    if (b.Key >= sinceHour) { r.Week += b.Value.Weight; r.Tokens += b.Value.Tokens; }
                    if (b.Key >= todayHour) r.Today += b.Value.Weight;
                }
                if (r.Week > 0) rows.Add(r);
            }
            return rows.ToArray();
        }
    }
}
'@

function Initialize-TranscriptUsage {
    if ($NoProjectUsage) { return }
    try {
        Add-Type -TypeDefinition $TranscriptUsageSource -ErrorAction Stop
        $script:transcriptOn = $true
        Log "projects: reading Claude Code's transcripts in $TranscriptsPath for the usage by project"
    } catch {
        $script:transcriptNote = "reader unavailable: $($_.Exception.Message)"
        Log "projects: transcript reader unavailable: $($_.Exception.Message)"
    }
}

# Shares of the weekly limit per project: the top five and the rest
function Update-ProjectUsage {
    if (-not $script:transcriptOn) { return }
    $w = $script:usage['weekly_all']
    if (-not $w -or -not $w.resetsAt) { $script:usageProjects = @(); return }
    $rows = [TranscriptUsage]::Totals($w.resetsAt.AddDays(-7).UtcDateTime, [DateTimeOffset]::new((Get-Date).Date).UtcDateTime)
    $by = @{}
    $total = 0.0
    foreach ($r in $rows) {
        $name = if ($r.Root) { Get-ProjectName $r.Root } else { [string]$r.Folder }
        if (-not $by.ContainsKey($name)) { $by[$name] = @{ name = $name; weight = 0.0; todayWeight = 0.0; tokens = [int64]0 } }
        $by[$name].weight += $r.Week
        $by[$name].todayWeight += $r.Today
        $by[$name].tokens += $r.Tokens
        $total += $r.Week
    }
    if ($total -le 0) { $script:usageProjects = @(); return }
    # Claude Code's part of the weekly limit: the account's split, else all of it
    $cc = $script:usageSplit | Where-Object { $_.key -eq 'claude_code' -or $_.label -eq 'Claude Code' } | Select-Object -First 1
    $part = $w.percent * $(if ($cc) { $cc.percent / 100 } else { 1.0 }) / $total
    $sorted = @($by.Values | Sort-Object { $_.weight } -Descending)
    $list = [System.Collections.Generic.List[object]]::new()
    foreach ($p in ($sorted | Select-Object -First 5)) {
        $list.Add(@{ name = $p.name; week = $p.weight * $part; today = $p.todayWeight * $part; tokens = $p.tokens })
    }
    $rest = @($sorted | Select-Object -Skip 5)
    if ($rest.Count) {
        $o = @{ name = 'other'; week = 0.0; today = 0.0; tokens = [int64]0 }
        foreach ($p in $rest) { $o.week += $p.weight * $part; $o.today += $p.todayWeight * $part; $o.tokens += $p.tokens }
        $list.Add($o)
    }
    $script:usageProjects = $list.ToArray()
}

# The reader's scan finished (a minute apart, on a pool thread)
function Complete-TranscriptScan {
    $t = $script:transcriptTask
    $script:transcriptTask = $null
    if ($t.IsFaulted) {
        $script:transcriptNote = "scan failed: $($t.Exception.GetBaseException().Message)"
        return
    }
    $err = [TranscriptUsage]::LastError
    $script:transcriptNote = '{0} files read in {1:0} ms{2}' -f [TranscriptUsage]::FilesRead, [TranscriptUsage]::LastScanMs, $(if ($err) { "; last error: $err" })
    Update-ProjectUsage
}

# What the status line shows: model, context, and the two windows
function Format-StatusLineText($j) {
    $parts = @()
    if ($j.model.display_name) { $parts += [string]$j.model.display_name }
    if ($null -ne $j.context_window.used_percentage) { $parts += 'ctx {0:0}%' -f [double]$j.context_window.used_percentage }
    $s = $script:usage['session']
    if ($s) {
        $t = '5h {0:0}%' -f $s.percent
        if ($s.resetsAt) { $t += ' (resets {0:HH:mm})' -f $s.resetsAt.LocalDateTime }
        $parts += $t
    }
    $w = $script:usage['weekly_all']
    if ($w) { $parts += '7d {0:0}%' -f $w.percent }
    return ($parts -join ' · ')
}

# ---------------- the Claude desktop app's notifications ----------------

$DesktopId = 'claude-desktop'
$script:desktopOn     = $false
$script:desktopNote   = $(if ($DesktopNotifications) { 'starting' } else { 'off (no -DesktopNotifications)' })
$script:desktopDb     = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Microsoft\Windows\Notifications\wpndatabase.db'
$script:desktopSeen   = @{}   # notification id -> $true, for the ones still in the database
$script:desktopWait   = @{}   # notification id -> arrival, for the ones that need you
$script:desktopNextAt = [datetime]::MinValue
$script:desktopFocusSince = [datetime]::MinValue
$script:desktopIdleSince  = [datetime]::MinValue
$script:desktopLast   = $null # the last one: @{ time; title; state }

$desktopSource = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

// The Claude desktop app's toasts from the Action Center's database, read
// with the SQLite that ships with Windows, and the program owning the
// foreground window
public static class DesktopToasts {
    [DllImport("winsqlite3.dll", EntryPoint = "sqlite3_open_v2")]
    static extern int Open(byte[] filename, out IntPtr db, int flags, IntPtr vfs);
    [DllImport("winsqlite3.dll", EntryPoint = "sqlite3_close_v2")]
    static extern int Close(IntPtr db);
    [DllImport("winsqlite3.dll", EntryPoint = "sqlite3_busy_timeout")]
    static extern int BusyTimeout(IntPtr db, int ms);
    [DllImport("winsqlite3.dll", EntryPoint = "sqlite3_prepare_v2")]
    static extern int Prepare(IntPtr db, byte[] sql, int nByte, out IntPtr stmt, IntPtr tail);
    [DllImport("winsqlite3.dll", EntryPoint = "sqlite3_step")]
    static extern int Step(IntPtr stmt);
    [DllImport("winsqlite3.dll", EntryPoint = "sqlite3_finalize")]
    static extern int Finalize(IntPtr stmt);
    [DllImport("winsqlite3.dll", EntryPoint = "sqlite3_column_int64")]
    static extern long ColumnInt64(IntPtr stmt, int col);
    [DllImport("winsqlite3.dll", EntryPoint = "sqlite3_column_blob")]
    static extern IntPtr ColumnBlob(IntPtr stmt, int col);
    [DllImport("winsqlite3.dll", EntryPoint = "sqlite3_column_bytes")]
    static extern int ColumnBytes(IntPtr stmt, int col);

    [DllImport("user32.dll")]
    static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")]
    static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("kernel32.dll")]
    static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    static extern bool QueryFullProcessImageNameW(IntPtr process, int flags, StringBuilder name, ref int size);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr handle);

    public class Toast {
        public long Id;
        public long Arrival;      // FILETIME, UTC
        public string Payload;    // the toast's XML
    }

    // The Store app is "Claude_<publisher>!Claude"; the older installer's
    // ids carry "AnthropicClaude"
    const string Sql =
        "SELECT n.Id, n.ArrivalTime, n.Payload FROM Notification n " +
        "JOIN NotificationHandler h ON h.RecordId = n.HandlerId " +
        "WHERE h.PrimaryId LIKE 'Claude%!Claude' OR h.PrimaryId LIKE '%AnthropicClaude%' ORDER BY n.Id";

    public static List<Toast> Read(string path) {
        var list = new List<Toast>();
        IntPtr db;
        int rc = Open(Encoding.UTF8.GetBytes(path + "\0"), out db, 1 /* SQLITE_OPEN_READONLY */, IntPtr.Zero);
        if (rc != 0) {
            if (db != IntPtr.Zero) Close(db);
            throw new Exception("sqlite open failed (" + rc + ")");
        }
        try {
            BusyTimeout(db, 250);
            IntPtr stmt;
            rc = Prepare(db, Encoding.UTF8.GetBytes(Sql + "\0"), -1, out stmt, IntPtr.Zero);
            if (rc != 0) throw new Exception("sqlite prepare failed (" + rc + ")");
            try {
                while (Step(stmt) == 100 /* SQLITE_ROW */) {
                    var t = new Toast { Id = ColumnInt64(stmt, 0), Arrival = ColumnInt64(stmt, 1), Payload = "" };
                    int n = ColumnBytes(stmt, 2);
                    IntPtr p = ColumnBlob(stmt, 2);
                    if (n > 0 && p != IntPtr.Zero) {
                        var bytes = new byte[n];
                        Marshal.Copy(p, bytes, 0, n);
                        t.Payload = Encoding.UTF8.GetString(bytes).TrimStart('﻿', '\0');
                    }
                    list.Add(t);
                }
            } finally {
                Finalize(stmt);
            }
        } finally {
            Close(db);
        }
        return list;
    }

    // Full path of the program whose window is in front, or ""
    public static string ForegroundImage() {
        IntPtr w = GetForegroundWindow();
        if (w == IntPtr.Zero) return "";
        uint pid;
        GetWindowThreadProcessId(w, out pid);
        IntPtr h = OpenProcess(0x1000 /* PROCESS_QUERY_LIMITED_INFORMATION */, false, pid);
        if (h == IntPtr.Zero) return "";
        try {
            var sb = new StringBuilder(1024);
            int size = sb.Capacity;
            return QueryFullProcessImageNameW(h, 0, sb, ref size) ? sb.ToString() : "";
        } finally {
            CloseHandle(h);
        }
    }
}
'@

function Set-DesktopNote([string]$note) {
    if ($note -ne $script:desktopNote) { Log "desktop: $note" }
    $script:desktopNote = $note
}

# Starts watching, taking what is already in the Action Center as seen
# (except the last five minutes, so a restart does not miss a fresh one)
function Initialize-DesktopWatch {
    if (-not $DesktopNotifications) { return }
    if (-not $IsWindows) { Set-DesktopNote 'not Windows: off'; return }
    if (-not (Test-Path -LiteralPath $script:desktopDb)) { Set-DesktopNote "no notification database at $($script:desktopDb)"; return }
    try {
        Add-Type -TypeDefinition $desktopSource -ErrorAction Stop
        $recent = (Get-Date).AddMinutes(-5)
        foreach ($t in [DesktopToasts]::Read($script:desktopDb)) {
            if ([DateTime]::FromFileTimeUtc($t.Arrival).ToLocalTime() -lt $recent) { $script:desktopSeen[$t.Id] = $true }
        }
        $script:desktopOn = $true
        Set-DesktopNote "watching the Claude app's notifications ($($script:desktopSeen.Count) already there)"
    } catch {
        Set-DesktopNote "cannot read notifications: $($_.Exception.Message)"
    }
}

function Get-ToastTexts([string]$payload) {
    $out = @()
    try {
        $x = [xml]$payload
        foreach ($n in $x.SelectNodes('//text')) { $t = $n.InnerText.Trim(); if ($t) { $out += $t } }
    } catch {}
    if (-not $out) { $out = @('(notification)') }
    return ,$out
}

# One notification as an event line for the tile's inspector
function Format-DesktopEvent([string]$title, [string]$body, [string]$state, [datetime]$arrival) {
    $e = [pscustomobject][ordered]@{
        hook_event_name = 'DesktopNotification'; session_id = $DesktopId
        title = $title; body = $body; state = $state; arrived = $arrival.ToString('HH:mm:ss'); app = 'Claude Desktop'
    }
    return (Format-EventLine $e 'Notification/desktop' 'Claude Desktop' '')
}

# Keeps the "Claude Desktop" session in step with the notifications: red
# while one needs you, then the green check, gone after DesktopLingerMinutes
function Sync-DesktopSession([bool]$fresh) {
    $now = Get-Date
    $exists = $script:sessions.ContainsKey($DesktopId)
    if (-not $exists -and -not $fresh) { return }
    $s = Get-Session $DesktopId
    if (-not $exists) { $s.project = 'Claude Desktop'; $s.root = ''; $s.first = $now }
    $want = if ($script:desktopWait.Count -gt 0) { 'waiting_user' } else { 'idle' }
    if ($fresh -or $want -ne $s.state) {
        if ($want -eq 'idle') { $script:desktopIdleSince = $now }
        $s.state = $want
        $s.last = $now
        Sync-Device
    } elseif ($want -eq 'idle' -and ($now - $script:desktopIdleSince).TotalMinutes -ge $DesktopLingerMinutes) {
        $script:sessions.Remove($DesktopId)
        $script:desktopLast = $null
        Log 'desktop: tile removed'
        Sync-Device
    }
}

function Update-DesktopToasts {
    $now = Get-Date
    try {
        $toasts = [DesktopToasts]::Read($script:desktopDb)
    } catch {
        Set-DesktopNote "read failed: $($_.Exception.Message)"
        return
    }
    $present = @{}
    $lines = @()
    foreach ($t in $toasts) {
        $present[$t.Id] = $true
        if ($script:desktopSeen.ContainsKey($t.Id)) { continue }
        $script:desktopSeen[$t.Id] = $true
        $arrival = [DateTime]::FromFileTimeUtc($t.Arrival).ToLocalTime()
        $texts = Get-ToastTexts $t.Payload
        $title = $texts[0]
        $body = (@($texts | Select-Object -Skip 1)) -join ' '
        $needsYou = "$title $body" -match 'approv|permission|needs? your|waiting for you|input needed|requires your'
        $state = if ($needsYou) { 'waiting_user' } else { 'idle' }
        if ($needsYou) {
            $script:desktopWait[$t.Id] = $arrival
            $script:desktopFocusSince = [datetime]::MinValue
        }
        $script:desktopLast = @{ time = $arrival.ToString('s'); title = $title; state = $state }
        Log "desktop: '$title' -> $state"
        $line = Format-DesktopEvent $title $body $state $arrival
        Add-Event 'Notification/desktop' $line
        $lines += $line
    }
    foreach ($id in @($script:desktopSeen.Keys)) { if (-not $present.ContainsKey($id)) { $script:desktopSeen.Remove($id) } }

    # The red sign goes when its notification leaves the Action Center
    # (clicked, dismissed), after DesktopWaitMinutes, or once the app's
    # window has been in front for three seconds
    foreach ($id in @($script:desktopWait.Keys)) {
        if (-not $present.ContainsKey($id) -or ($now - $script:desktopWait[$id]).TotalMinutes -ge $DesktopWaitMinutes) {
            $script:desktopWait.Remove($id)
        }
    }
    if ($script:desktopWait.Count -gt 0) {
        if ([DesktopToasts]::ForegroundImage() -match '\\WindowsApps\\Claude_|\\AnthropicClaude\\') {
            if ($script:desktopFocusSince -eq [datetime]::MinValue) { $script:desktopFocusSince = $now }
            elseif (($now - $script:desktopFocusSince).TotalSeconds -ge 3) {
                $script:desktopWait.Clear()
                Log 'desktop: the app has been in front: taken as seen'
            }
        } else {
            $script:desktopFocusSince = [datetime]::MinValue
        }
    }

    Sync-DesktopSession ($lines.Count -gt 0)
    foreach ($l in $lines) { Send-Event $l }   # after the session line: the tile exists
}

# ---------------- http ----------------

function Send-Response($ctx, [int]$code, [string]$body, [string]$type = 'application/json') {
    try {
        $ctx.Response.StatusCode  = $code
        $ctx.Response.ContentType = $type
        $bytes = [Text.Encoding]::UTF8.GetBytes($body)
        $ctx.Response.ContentLength64 = $bytes.Length
        $ctx.Response.OutputStream.Write($bytes, 0, $bytes.Length)
        $ctx.Response.Close()
    } catch {}
}

function Handle-Request($ctx) {
    $req  = $ctx.Request
    $path = $req.Url.AbsolutePath
    $body = ''
    if ($req.HasEntityBody) {
        $body = [IO.StreamReader]::new($req.InputStream, [Text.Encoding]::UTF8).ReadToEnd()
    }

    if ($path -eq '/hook') {
        try {
            $e = $body | ConvertFrom-Json
            $rootHeader = [string]$req.Headers['X-Claude-Project']
            # The client process, looked up only while the session has none (or a dead one)
            $clientPid = 0
            $sid = [string]$e.session_id
            if ($sid) {
                $known = if ($script:sessions.ContainsKey($sid)) { $script:sessions[$sid] } else { $null }
                if (-not $known -or $known.pid -le 0 -or -not (Test-ProcessAlive $known.pid)) { $clientPid = Resolve-ClientPid $req }
            }
            Process-Hook $e $rootHeader $clientPid
            Send-Response $ctx 200 '{}'
        } catch {
            Log "hook: bad payload: $($_.Exception.Message)"
            Send-Response $ctx 400 '{"error":"bad json"}'
        }
        return
    }
    if ($path -eq '/statusline') {
        # The status line command POSTs its input here and prints the answer
        $text = ''
        try {
            $j = $body | ConvertFrom-Json
            if ($j.rate_limits -and (Set-UsageFromStatusLine $j.rate_limits)) { Send-Usage }
            $text = Format-StatusLineText $j
        } catch {
            Log "statusline: bad payload: $($_.Exception.Message)"
        }
        Send-Response $ctx 200 $text 'text/plain; charset=utf-8'
        return
    }
    if ($path -eq '/usage') {
        Send-Response $ctx 200 ((Get-UsageObject) | ConvertTo-Json -Depth 4)
        return
    }
    if ($path -match '^/state/([a-z_]+)$') {
        $st = $Matches[1]
        if ($st -notin $States) { Send-Response $ctx 400 '{"error":"unknown state"}'; return }
        $ok = Send-Serial $st
        if ($ok) {
            $script:deviceState = $st
            if ($st -in 'idle', 'off', 'error', 'paused') { $script:deviceSubagents = 0 }
        }
        Log "manual -> $st (sent=$ok)"
        Send-Response $ctx 200 (@{ state = $st; sent = $ok } | ConvertTo-Json -Compress)
        return
    }
    if ($path -match '^/serial/([a-z_]+)$') {
        $c = $Matches[1]
        $reply = Send-SerialCommand $c
        Send-Response $ctx 200 (@{ sent = $c; reply = $reply } | ConvertTo-Json -Compress)
        return
    }
    if ($path -eq '/events') {
        $n = 20
        if ($req.QueryString['n']) { $n = [int]$req.QueryString['n'] }
        $take = [Math]::Min($n, $script:events.Count)
        $items = @(if ($take -gt 0) { $script:events.GetRange($script:events.Count - $take, $take) })
        [array]::Reverse($items)
        $obj = [ordered]@{ count = $script:events.Count; sent_to_device = $script:eventsSent; events = $items }
        Send-Response $ctx 200 ($obj | ConvertTo-Json -Depth 4)
        return
    }
    if ($path -eq '/version') {
        $reply = Read-DeviceInfo
        $obj = [ordered]@{
            port   = $PortName
            reply  = $reply
            device = $script:deviceInfo
            daemon = [ordered]@{ script = $PSCommandPath; started = $script:started.ToString('s'); pwsh = $PSVersionTable.PSVersion.ToString() }
        }
        Send-Response $ctx 200 ($obj | ConvertTo-Json -Depth 4)
        return
    }
    if ($path -eq '/calibrate') {
        $q = $req.QueryString
        $fields = @()
        if ($q['reset']) { $fields = @('reset') }
        else {
            foreach ($k in 'rot', 'sign', 'offset') { if ($null -ne $q[$k] -and $q[$k] -ne '') { $fields += "$k=$($q[$k])" } }
        }
        if ($fields.Count -eq 0) { Send-Response $ctx 400 '{"error":"give rot, sign, offset or reset"}'; return }
        $c = "calibrate $($fields -join ' ')"
        $reply = Send-SerialCommand $c
        Log "calibrate -> $c (reply: $reply)"
        Send-Response $ctx 200 (@{ sent = $c; reply = $reply } | ConvertTo-Json -Compress)
        return
    }
    if ($path -eq '/release') {
        $sec = 120
        if ($req.QueryString['seconds']) { $sec = [int]$req.QueryString['seconds'] }
        $script:releaseUntil = (Get-Date).AddSeconds($sec)
        Close-Serial "released for $sec s"
        Send-Response $ctx 200 (@{ released_seconds = $sec } | ConvertTo-Json -Compress)
        return
    }
    if ($path -eq '/reconnect') {
        $script:releaseUntil = [datetime]::MinValue
        $script:nextOpenTry  = [datetime]::MinValue
        Send-Response $ctx 200 '{}'
        return
    }
    if ($path -eq '/status') {
        $obj = [ordered]@{
            port             = $PortName
            serial_open      = [bool]$script:serial
            released_until   = $(if ($script:releaseUntil -gt (Get-Date)) { $script:releaseUntil.ToString('s') } else { $null })
            started          = $script:started.ToString('s')
            device_info      = $script:deviceInfo
            events_kept      = $script:events.Count
            events_sent      = $script:eventsSent
            event_log        = $(if ($LogEvents) { $EventLogPath } else { $null })
            device_state     = $script:deviceState
            device_subagents = $script:deviceSubagents
            device_sessions  = $script:deviceSessions
            effective        = (Get-EffectiveState)
            codes            = (Get-SessionCodes)
            last_ping        = $(if ($script:lastPing -gt [datetime]::MinValue) { $script:lastPing.ToString('s') } else { $null })
            process_watch    = $script:procSnap
            process_pids     = (Get-SessionPids)
            device_tool      = $script:deviceTool
            usage            = (Get-UsageObject)
            desktop          = [ordered]@{ watching = $script:desktopOn; note = $script:desktopNote; waiting = $script:desktopWait.Count; last = $script:desktopLast }
            sessions         = @($script:sessions.GetEnumerator() | ForEach-Object {
                [ordered]@{ id = $_.Key; state = $_.Value.state; subagents = $_.Value.subagents; last = $_.Value.last.ToString('s'); project = $_.Value.project; root = $_.Value.root; pid = $_.Value.pid; tools = @($_.Value.tools.GetEnumerator() | ForEach-Object { "$($_.Value) ($($_.Key))" }) }
            })
            projects         = $ProjectsPath
            log              = $LogPath
        }
        Send-Response $ctx 200 ($obj | ConvertTo-Json -Depth 5)
        return
    }
    if ($path -eq '/health') { Send-Response $ctx 200 '{"ok":true}'; return }
    Send-Response $ctx 404 '{"error":"not found"}'
}

# ---------------- main ----------------

$listener = [System.Net.HttpListener]::new()
$listener.Prefixes.Add("http://localhost:$HttpPort/")
try {
    $listener.Start()
} catch {
    Log "http: cannot listen on port ${HttpPort}: $($_.Exception.Message)"
    exit 1
}
Log "daemon started: port=$PortName http=http://localhost:$HttpPort/ log=$LogPath"

# Started by the scheduled task as `conhost --headless pwsh ...`: stopping the
# task ends that conhost only, so follow it out instead of running on as an
# orphan that keeps the port
$script:launcher = $null
try {
    $parent = (Get-Process -Id $PID).Parent
    if ($parent -and $parent.ProcessName -eq 'conhost') { $script:launcher = $parent }
} catch {}

Update-ProjectNames
Initialize-ProcessSnapshots
Initialize-DesktopWatch
Initialize-UsageHistory
Initialize-TranscriptUsage
Open-Serial | Out-Null
$task = $listener.GetContextAsync()
$lastHousekeeping = Get-Date

try {
    while ($true) {
        $got = $false
        try { $got = $task.Wait(250) } catch { Log "http: listener error: $($_.Exception.Message)"; break }
        if ($got) {
            $ctx  = $task.Result
            $task = $listener.GetContextAsync()
            try {
                Handle-Request $ctx
            } catch {
                Log "http: handler error: $($_.Exception.Message)"
                Send-Response $ctx 500 '{"error":"internal"}'
            }
        }

        if ($script:launcher -and $script:launcher.HasExited) {
            Log "launcher conhost (pid $($script:launcher.Id)) has exited: stopping"
            break
        }

        $now = Get-Date
        if ($script:procSnap -and $now -ge $script:procNextAt) {
            # Twice a second while an approval may be pending or a tool was just
            # called (its process is about to appear), every two seconds otherwise
            $busy = @($script:sessions.Values | Where-Object {
                $_.pid -gt 0 -and ($_.state -eq 'waiting_user' -or ($now - $_.last).TotalSeconds -lt 5) }).Count -gt 0
            $script:procNextAt = $now.AddMilliseconds($(if ($busy) { 500 } else { 2000 }))
            if (Update-ProcessTree) {
                Expire-Sessions
                Sync-Device
            }
        }
        if (-not $script:serial -and $now -ge $script:releaseUntil -and $now -ge $script:nextOpenTry) {
            if (Open-Serial) {
                # The device may have rebooted: push everything again
                $script:deviceState = $null
                $script:deviceSubagents = 0
                $script:deviceSessions = $null
                $script:deviceTool = $false
                $script:deviceSessionLines = @{}
                $script:deviceUsageKey = $null
                $script:lastInfoTry = $now
                Read-DeviceInfo | Out-Null
                Sync-Device $true
                if (Send-Serial 'ping') { $script:lastPing = $now }
            } else {
                $script:nextOpenTry = $now.AddSeconds(2)
            }
        }
        if ($script:serial -and ($now - $script:lastPing).TotalSeconds -ge $PingSeconds) {
            if (Send-Serial 'ping') { $script:lastPing = $now }
        }
        # A device that was still booting when the port opened (the Tab5
        # takes ~2.5 s to bring its panel up) answered nothing: ask again.
        # Also the first question after the daemon starts. Once it answers,
        # push everything from session_clear on: its tiles may be from an
        # earlier daemon (sessions that ended meanwhile) or a demo.
        if ($script:serial -and -not $script:deviceInfo -and ($now - $script:lastInfoTry).TotalSeconds -ge 10) {
            $script:lastInfoTry = $now
            Read-DeviceInfo | Out-Null
            if ($script:deviceInfo) { Sync-Device $true }
        }
        foreach ($line in (Read-SerialLines)) {
            if ($line -match 'STATUS|VERSION|ERROR') { Log "device: $($line.Trim())" }
        }
        # Plan usage: the account poll (in flight without blocking the loop),
        # and the device's copy kept current (a device that announced its
        # features late, a window that reset)
        if ($AccountUsage -and -not $script:usageTask -and $now -ge $script:usageNextPoll) { Start-UsagePoll }
        if ($script:usageTask -and $script:usageTask.IsCompleted) { Complete-UsagePoll }
        if ($script:profileTask -and $script:profileTask.IsCompleted) { Complete-ProfilePoll }
        if ($script:transcriptOn -and -not $script:transcriptTask -and $now -ge $script:transcriptNextAt) {
            $script:transcriptNextAt = $now.AddSeconds(60)
            $script:transcriptTask = [TranscriptUsage]::ScanAsync($TranscriptsPath, 8)
        }
        if ($script:transcriptTask -and $script:transcriptTask.IsCompleted) { Complete-TranscriptScan }
        if ($now -ge $script:usageCheckAt) {
            $script:usageCheckAt = $now.AddSeconds(5)
            Send-Usage
        }
        # The desktop app's notifications, every three seconds
        if ($script:desktopOn -and $now -ge $script:desktopNextAt) {
            $script:desktopNextAt = $now.AddSeconds(3)
            Update-DesktopToasts
        }
        if (($now - $lastHousekeeping).TotalSeconds -ge 60) {
            Expire-Sessions
            Sync-Device   # sends only what changed: a session gone, a compaction given up
            Update-ProjectNames
            $lastHousekeeping = $now
        }
    }
} finally {
    Close-Serial "shutdown"
    try { $listener.Stop() } catch {}
    Log "daemon stopped"
}
