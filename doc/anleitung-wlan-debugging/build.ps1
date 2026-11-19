# Erzeugt wlan-debugging.html und wlan-debugging.pdf (asciidoctor aus C:\localapps\ruby).
$ErrorActionPreference = 'Stop'
$bin = 'C:\localapps\ruby\bin'
$env:PATH = "$bin;$env:PATH"
Push-Location $PSScriptRoot
try {
    & "$bin\asciidoctor.bat" -o wlan-debugging.html wlan-debugging.adoc
    & "$bin\asciidoctor-pdf.bat" -o wlan-debugging.pdf wlan-debugging.adoc
    Get-Item wlan-debugging.html, wlan-debugging.pdf | Select-Object Name, Length
} finally {
    Pop-Location
}
