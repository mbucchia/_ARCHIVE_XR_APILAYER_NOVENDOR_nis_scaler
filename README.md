# OpenXR API Layer for NVIDIA Image Scaling (NIS)

This software inserts an NVIDIA Image Scaling (NIS) shader in the frame submission path of any OpenXR application.

The NIS scaling can be used instead of the application render scale setting (if available) or the OpenXR custom render scale to either:

- Achieve better visual quality without lowering performance;
- Achieve better performance with minimal visual quality loss (compared to the traditional render scale settings).

DISCLAIMER: This software is distributed as-is, without any warranties or conditions of any kind. Use at your own risks.

## Requirements

This software may be used with any brand of VR headset as long as the target application is developed using OpenXR. The software has been confirmed to work with Windows Mixed Reality, Valve Index, HTC Vive, Pimax and Oculus Quest.
This software may be used with any modern GPU, and is not limited to NVIDIA hardware.

## Installation

Please visit the release page to download the installer for the latest version: https://github.com/mbucchia/XR_APILAYER_NOVENDOR_nis_scaler/releases.

(For Windows Mixed Reality users) You may confirm that the software is properly installed/uninstalled by using the OpenXR Developer Tools for Windows Mixed Reality, check under the *System Status* tab, scroll down to *API Layers*. A layer named `XR_APILAYER_NOVENDOR_nis_scaler` is listed when the software is installed.

## Configuration file

NOTE TO MICROSOFT FLIGHT SIMULATOR 2020 USERS: The installer already provides a base configuration file (`FS2020.cfg`) for Flight Simulator 2020!

The configuration file contains two values:

* `scaling` (between 0 and 1), indicating the amount of upscaling to request;
* `sharpness` (between 0 and 1), indicating the the amount of sharpness filtering to apply.

The NIS scaling processing occurs after the in-game render scale scaling (if any) and before the OpenXR custom render scale scaling.

Example configuration file with a scaling factor of 65% and sharpness of 40%:

```
scaling=0.65
sharpness=0.4
```

## Best practices

(_This section is still under construction_)

## Creating the configuration file

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

5. Update the file with the designed settings.

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
