$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$repoRoot = Split-Path $PSScriptRoot -Parent
$image = [System.Drawing.Image]::FromFile((Join-Path $repoRoot 'assets/icon/snesdq5.png'))
$res = Join-Path $repoRoot 'android/app/src/main/res'
try {
    foreach ($entry in @(@('mdpi',1),@('hdpi',1.5),@('xhdpi',2),@('xxhdpi',3),@('xxxhdpi',4))) {
        $dir = Join-Path $res "mipmap-$($entry[0])"
        New-Item -ItemType Directory -Force $dir | Out-Null
        foreach ($adaptive in @($false,$true)) {
            $size = [int](($adaptive ? 108 : 48) * $entry[1])
            $bitmap = [System.Drawing.Bitmap]::new($size,$size)
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.Clear([System.Drawing.Color]::Transparent)
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $inset = $adaptive ? [int](18*$entry[1]) : 0
                $graphics.DrawImage($image,$inset,$inset,$size-2*$inset,$size-2*$inset)
                $name = $adaptive ? 'ic_launcher_foreground.png' : 'ic_launcher.png'
                $bitmap.Save((Join-Path $dir $name),[System.Drawing.Imaging.ImageFormat]::Png)
            } finally { $graphics.Dispose(); $bitmap.Dispose() }
        }
    }
} finally { $image.Dispose() }
New-Item -ItemType Directory -Force (Join-Path $res 'mipmap-anydpi-v26'),(Join-Path $res 'values') | Out-Null
@'
<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
  <background android:drawable="@color/icon_background" />
  <foreground android:drawable="@mipmap/ic_launcher_foreground" />
</adaptive-icon>
'@ | Set-Content -LiteralPath (Join-Path $res 'mipmap-anydpi-v26/ic_launcher.xml') -Encoding utf8
@'
<?xml version="1.0" encoding="utf-8"?>
<resources><color name="icon_background">#071B3D</color></resources>
'@ | Set-Content -LiteralPath (Join-Path $res 'values/icon_colors.xml') -Encoding utf8
