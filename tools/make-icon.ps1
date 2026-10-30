Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root "assets\rec-lox.ico"
$bmp = New-Object System.Drawing.Bitmap 256, 256
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.Clear([System.Drawing.Color]::Transparent)
function Brush([int]$r,[int]$g,[int]$b) { return New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255,$r,$g,$b)) }
$dark = Brush 17 22 34
$panel = Brush 27 37 57
$line = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255,96,116,143)), 7
$lime = Brush 112 235 157
$red = Brush 255 83 100
$white = New-Object System.Drawing.Pen ([System.Drawing.Color]::White), 5
$path = New-Object System.Drawing.Drawing2D.GraphicsPath
$path.AddArc(8,8,112,112,180,90); $path.AddArc(136,8,112,112,270,90)
$path.AddArc(136,136,112,112,0,90); $path.AddArc(8,136,112,112,90,90); $path.CloseFigure()
$g.FillPath($dark,$path)
$g.DrawPath((New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255,57,70,93)),5),$path)
$g.FillRectangle($panel,45,75,166,110)
$g.DrawRectangle($line,45,57,166,128)
$g.FillEllipse($lime,93,93,70,70)
$g.FillEllipse($dark,111,111,34,34)
$g.FillEllipse($red,181,34,43,43)
$g.DrawEllipse($white,181,34,43,43)
$icon = [System.Drawing.Icon]::FromHandle($bmp.GetHicon())
$stream = [System.IO.File]::Open($out,[System.IO.FileMode]::Create)
$icon.Save($stream)
$stream.Dispose()
$icon.Dispose()
$g.Dispose(); $bmp.Dispose()
Write-Host "Created $out"
