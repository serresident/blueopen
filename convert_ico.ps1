$pngPath = "C:\Users\erch\.gemini\antigravity-ide\brain\1636785c-2af4-4722-84a9-ae7cc5599e96\blueopen_icon_1782941040423.png"
$icoPath = "BlueOpenServer\app_icon.ico"
$pngBytes = [System.IO.File]::ReadAllBytes($pngPath)
$fs = [System.IO.File]::Create($icoPath)
$bw = New-Object System.IO.BinaryWriter($fs)

$bw.Write([int16]0)
$bw.Write([int16]1)
$bw.Write([int16]1)
$bw.Write([byte]0)
$bw.Write([byte]0)
$bw.Write([byte]0)
$bw.Write([byte]0)
$bw.Write([int16]1)
$bw.Write([int16]32)
$bw.Write([int]$pngBytes.Length)
$bw.Write([int]22)
$bw.Write($pngBytes)

$bw.Close()
$fs.Close()
