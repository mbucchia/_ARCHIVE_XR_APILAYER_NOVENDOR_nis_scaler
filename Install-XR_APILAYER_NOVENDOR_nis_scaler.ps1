$JsonPath = Join-Path "$PSScriptRoot" "XR_APILAYER_NOVENDOR_nis_scaler.json"
Start-Process -FilePath powershell.exe -Verb RunAs -Wait -ArgumentList @"
	& {
		New-ItemProperty -Path HKLM:\Software\Khronos\OpenXR\1\ApiLayers\Implicit -Name '$jsonPath' -PropertyType DWord -Value 0 -Force | Out-Null
	}
"@
