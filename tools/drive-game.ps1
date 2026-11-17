# Drives the running game for checks and screenshots:
#   drive-game.ps1 <command> [args]...
# Start the game first and write its process id to pid.txt next to this
# script, for example:
#   $p = Start-Process build/build/Release/mechapunk.exe -ArgumentList "--new","--save","scratch.json" `
#        -WorkingDirectory build/build/Release -RedirectStandardOutput tools/out.txt -PassThru
#   $p.Id | Set-Content tools/pid.txt
# Keys (VK, scan): space 0x20 0x39, 1-5 speeds 0x31 0x02 .. 0x35 0x06,
# Q 0x51 0x10, E 0x45 0x12, Esc 0x1B 0x01. `shot` reads the saved name from
# out.txt, the game's redirected output.
# Commands (several may follow each other):
#   click X Y      left click at client coordinates
#   wheel N        wheel N notches (negative zooms out) at the window center
#   drag X1 Y1 X2 Y2  right-button drag between client points, which orbits
#   key VK SCAN    press a key by virtual key and scan code
#   shot           F12 screenshot, prints the saved file name
#   wait MS
param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $commands)
$scratch = Split-Path -Parent $MyInvocation.MyCommand.Path
$procId = [int](Get-Content (Join-Path $scratch "pid.txt"))
$p = Get-Process -Id $procId
Add-Type -AssemblyName Microsoft.VisualBasic
Add-Type @"
using System; using System.Runtime.InteropServices;
public class Drv { [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, UIntPtr e);
 [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, UIntPtr e);
 [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
 [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; } }
"@
[Drv]::SetProcessDPIAware() | Out-Null
[Microsoft.VisualBasic.Interaction]::AppActivate($p.Id) | Out-Null
Start-Sleep -Milliseconds 300
function MoveTo([int]$x, [int]$y) {
    $pt = New-Object Drv+POINT; $pt.X = $x; $pt.Y = $y
    [Drv]::ClientToScreen($p.MainWindowHandle, [ref]$pt) | Out-Null
    [Drv]::SetCursorPos($pt.X, $pt.Y) | Out-Null
    Start-Sleep -Milliseconds 150
}
function Press([byte]$vk, [byte]$scan) {
    [Drv]::keybd_event($vk, $scan, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60
    [Drv]::keybd_event($vk, $scan, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 200
}
$out = Join-Path $scratch "out.txt"
$i = 0
while ($i -lt $commands.Count) {
    switch ($commands[$i]) {
        "click" { MoveTo ([int]$commands[$i + 1]) ([int]$commands[$i + 2])
                  [Drv]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60
                  [Drv]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 400; $i += 3 }
        "dclick" { MoveTo ([int]$commands[$i + 1]) ([int]$commands[$i + 2])
                  for ($k = 0; $k -lt 2; $k++) { [Drv]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 30
                  [Drv]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60 }
                  Start-Sleep -Milliseconds 400; $i += 3 }
        "drag"  { MoveTo ([int]$commands[$i + 1]) ([int]$commands[$i + 2])
                  [Drv]::mouse_event(8, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60
                  for ($k = 1; $k -le 20; $k++) {
                      $x = [int]$commands[$i + 1] + (([int]$commands[$i + 3] - [int]$commands[$i + 1]) * $k / 20)
                      $y = [int]$commands[$i + 2] + (([int]$commands[$i + 4] - [int]$commands[$i + 2]) * $k / 20)
                      $pt = New-Object Drv+POINT; $pt.X = $x; $pt.Y = $y
                      [Drv]::ClientToScreen($p.MainWindowHandle, [ref]$pt) | Out-Null
                      [Drv]::SetCursorPos($pt.X, $pt.Y) | Out-Null; Start-Sleep -Milliseconds 20 }
                  [Drv]::mouse_event(0x10, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 300; $i += 5 }
        "wheel" { MoveTo 700 500; $n = [int]$commands[$i + 1]
                  $d = if ($n -lt 0) { -120 } else { 120 }
                  for ($k = 0; $k -lt [Math]::Abs($n); $k++) { [Drv]::mouse_event(0x0800, 0, 0, [uint32]([int64]$d -band 0xFFFFFFFFL), [UIntPtr]::Zero); Start-Sleep -Milliseconds 60 }
                  Start-Sleep -Milliseconds 300; $i += 2 }
        "key"   { Press ([byte]$commands[$i + 1]) ([byte]$commands[$i + 2]); $i += 3 }
        "shot"  { Press 0x7B 0x58; Start-Sleep -Milliseconds 800
                  (Get-Content $out | Select-String "Screenshot saved" | Select-Object -Last 1).ToString(); $i += 1 }
        "wait"  { Start-Sleep -Milliseconds ([int]$commands[$i + 1]); $i += 2 }
        default { throw "unknown command $($commands[$i])" }
    }
}
