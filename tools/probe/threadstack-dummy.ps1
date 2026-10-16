# Dummy for threadstack-test.py: a window owned by the UI thread (busy on a 5 ms timer) and one spinning worker thread.
Add-Type -AssemblyName System.Windows.Forms
$code = @"
using System; using System.Threading;
public static class Spin { public static void Start() { var t = new Thread(() => { double x = 0; while (true) { for (int i = 0; i < 1000000; i++) x += Math.Sqrt(i); Thread.Sleep(1); } }); t.IsBackground = true; t.Start(); } }
"@
Add-Type -TypeDefinition $code
[Spin]::Start()
$f = New-Object System.Windows.Forms.Form
$f.Text = 'mainstack-dummy'; $f.Width = 200; $f.Height = 100; $f.ShowInTaskbar = $false
$f.StartPosition = 'Manual'; $f.Location = New-Object System.Drawing.Point(-2000, -2000)
$t = New-Object System.Windows.Forms.Timer; $t.Interval = 5; $t.Add_Tick({ $s = 0; for ($i = 0; $i -lt 2000; $i++) { $s += $i } }); $t.Start()
[System.Windows.Forms.Application]::Run($f)
