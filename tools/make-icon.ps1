Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root "assets\rec-lox.ico"
$bmp = [System.Drawing.Bitmap]::new(256,256)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.Clear([System.Drawing.Color]::Transparent)
$dark = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255,17,22,34))
$panel = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255,27,37,57))
$lime = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255,112,235,157))
$red = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255,255,83,100))
$outline = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255,57,70,93),5)
$screenLine = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255,96,116,143),7)
$white = [System.Drawing.Pen]::new([System.Drawing.Color]::White,5)
$path = [System.Drawing.Drawing2D.GraphicsPath]::new()
$path.AddArc(8,8,112,112,180,90); $path.AddArc(136,8,112,112,270,90)
$path.AddArc(136,136,112,112,0,90); $path.AddArc(8,136,112,112,90,90); $path.CloseFigure()
$g.FillPath($dark,$path); $g.DrawPath($outline,$path)
$g.FillRectangle($panel,45,57,166,128); $g.DrawRectangle($screenLine,45,57,166,128)
$g.FillEllipse($lime,93,93,70,70); $g.FillEllipse($dark,111,111,34,34)
$g.FillEllipse($red,181,34,43,43); $g.DrawEllipse($white,181,34,43,43)
$hIcon = $bmp.GetHicon()
$icon = [System.Drawing.Icon]::FromHandle($hIcon)
$stream = [System.IO.File]::Open($out,[System.IO.FileMode]::Create)
$icon.Save($stream)
$stream.Dispose(); $icon.Dispose(); $bmp.Dispose(); $g.Dispose()
$path.Dispose(); $dark.Dispose(); $panel.Dispose(); $lime.Dispose(); $red.Dispose()
$outline.Dispose(); $screenLine.Dispose(); $white.Dispose()
Write-Host "Created $out"
