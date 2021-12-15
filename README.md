# OpenXR API Layer for NVIDIA Image Scaling (NIS)

This software inserts an NVIDIA Image Scaling (NIS) shader in the frame submission path of any OpenXR application.

The NIS scaling can be used instead of the application render scale setting (if available) or the OpenXR custom render scale to either:

- Achieve better visual quality without lowering performance;
- Achieve better performance with minimal visual quality loss (compared to the traditional render scale settings).

DISCLAIMER: This software is distributed as-is, without any warranties or conditions of any kind. Use at your own risks.

## Requirements

This software may be used with any brand of VR headset as long as the target application is developed using OpenXR. The software has been confirmed to work with Windows Mixed Reality, Valve Index, HTC Vive, Pimax and Oculus Quest.
This software may be used with any modern GPU, and is not limited to NVIDIA hardware.

## Download

A ZIP archive containing the necessary files to install and use the layer can be found on the release page: https://github.com/mbucchia/XR_APILAYER_NOVENDOR_nis_scaler/releases.

## Setup

1. Create a folder in `%ProgramFiles%`. It's important to make it in `%ProgramFiles%` so that UWP applications can access it! For example: `C:\Program Files\OpenXR-API-Layers`.

2. Place the files extracted from the ZIP archive (`XR_APILAYER_NOVENDOR_nis_scaler.json`, `XR_APILAYER_NOVENDOR_nis_scaler.dll`, `NIS_Main.hlsl`, `NIS_Scaler.h`, `Install-XR_APILAYER_NOVENDOR_nis_scaler.ps1` and `Uninstall-XR_APILAYER_NOVENDOR_nis_scaler.ps1`) in the folder created above. Also copy any configuration file (eg: `FS2020.cfg`) to that folder.

3. Run the script `Install-XR_APILAYER_NOVENDOR_nis_scaler.ps1` by right-clicking on the file, then choosing "Run with PowerShell". You will be prompted for elevation (running as Administrator).

4. (Optional) Start the OpenXR Developer Tools for Windows Mixed Reality, under the *System Status* tab, scroll down to *API Layers*. A layer named `XR_APILAYER_NOVENDOR_nis_scaler` should be listed.

## Removal

1. Go to the folder where the API layer is installed. For example: `C:\Program Files\OpenXR-API-Layers`.

2. Run the script `Uninstall-XR_APILAYER_NOVENDOR_nis_scaler.ps1` by right-clicking on the file, then choosing "Run with PowerShell". You will be prompted for elevation (running as Administrator).

3. (Optional) Start the OpenXR Developer Tools for Windows Mixed Reality, under the *System Status* tab, scroll down to *API Layers*. There should be no layer named `XR_APILAYER_NOVENDOR_nis_scaler`.

## App configuration

NOTE TO Microsoft Flight Simulator 2020 USERS: The ZIP archive already contains a configuration file (`FS2020.cfg`) for Flight Simulator 2020! Just copy the file as part of Setup step 2) above!

In order to enable the software for a given application (eg: Microsoft Flight Simulator 2020 aka MSFS2020), a configuration file must be present for this application.

1. Each application registers itself with a name. The first step is to retrieve the name that the application passes to OpenXR. In order to do that, follow the setup instructions above to install the software, then run the application you wish to enable NIS scaling for. In this example, we start MSFS2020.

2. Locate the log file for the software. It will typically be stored at `%LocalAppData%\XR_APILAYER_NOVENDOR_nis_scaler.log`.

3. In the log file, search for the first line reading "Could not load config for ...". The name specified on this line is the application name:

```
dllHome is "C:\Program Files\OpenXR-API-Layers"
XR_APILAYER_NOVENDOR_nis_scaler layer is active
Could not load config for "FS2020"
Could not load config for "Zouna"
```

4. In the same folder where `XR_APILAYER_NOVENDOR_nis_scaler.json` was copied during setup, create a file with a name matching the application name and with the extension `.cfg`. For our example `C:\Program Files\OpenXR-API-Layers\FS2020.cfg`.

5. (Optional) In this configuration file, you may change the `scaling` value to specify the amount of upscaling to request (default is 0.7, which means 70%). You may also specify a value for `sharpness` between 0 and 1 (default is 0.5). Example configuration file with a scaling factor of 65% and sharpness of 40%:

```
scaling=0.65
sharpness=0.4
```

## Keyboard shortcuts

Changing the sharpness setting can be done in increments of 5% by pressing Ctrl + Down arrow (or Ctrl + F2) to decrease and Ctrl + Up arrow (or Ctrl + F3) to increase. The new sharpness value can be observed in the log file (typically stored at `%LocalAppData%\XR_APILAYER_NOVENDOR_nis_scaler.log`). When satisfied, you may then modify the configuration file corresponding to the application to make the setting permanent.

You can use Ctrl + Left arrow (or Ctrl + F1) to enable/disable NIS and switch to a cheap scaler (no filter) instead, so you can see the improvements (hopefully) that NIS provides.

Changing the render scale cannot be done via a keyboard shortcut and requires the VR session to be restarted with an updated configuration file.

## Limitations

This OpenXR API layer is currently very limited in the mode and input that it accepts:

* This software was only extensively tested with Microsoft Flight Simulator;
* Only Direct3D 11 is supported;
* Not all swapchain formats are supported (exact list TBD) and some properties are untested (arraySize, sampleCount);
* Cropped imageRect submission is not supported;
* Depth submission is not supported.

## Contributions

The author is Matthieu Bucchianeri (https://github.com/mbucchia/). Please note that this software is not affiliated with Microsoft.

Special thanks to BufordTX for submitting a fix for a bug in the installation script.

Special thanks to CptLucky8 for spotting an issue with the instructions.

Special thanks to HyperJet2018 and CptLucky8 for their input on resolving the sRGB color degrations issue.

Many thanks to the https://forums.flightsimulator.com/ community for the testing and feedback!
