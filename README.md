# OpenXR API Layer for NVIDIA Image Scaling (NIS)

This software inserts an NVIDIA Image Scaling (NIS) shader in the frame submission path of any OpenXR application.

## Download

A ZIP file containing the necessary files to install and use the layer can be found on the release page: https://github.com/mbucchia/XR_APILAYER_NOVENDOR_nis_scaler/releases.

## Setup

1. Create a folder in `%ProgramFiles%`. It's important to make it in `%ProgramFiles%` so that UWP applications can access it! For example: `C:\Program Files\OpenXR-API-Layers`.

2. Place `XR_APILAYER_NOVENDOR_nis_scaler.json`, `XR_APILAYER_NOVENDOR_nis_scaler.dll`, `NIS_Main.hlsl`, `NIS_Scaler.h`, `Install-XR_APILAYER_NOVENDOR_nis_scaler.ps1` and `Uninstall-XR_APILAYER_NOVENDOR_nis_scaler.ps1` in the folder created above. Also copy any configuration file (eg: `FS2020.cfg`) to that folder.

3. Run the script `Install-XR_APILAYER_NOVENDOR_nis_scaler.ps1`. You will be prompted for elevation (running as Administrator).

4. (Optional) Start the OpenXR Developer Tools for Windows Mixed Reality, under the *System Status* tab, scroll down to *API Layers*. A layer named `XR_APILAYER_NOVENDOR_nis_scaler` should be listed.

## Removal

1. Go to the folder where the API layer is installed. For example: `C:\Program Files\OpenXR-API-Layers`.

2. Run the script `Uninstall-XR_APILAYER_NOVENDOR_nis_scaler.ps1`. You will be prompted for elevation (running as Administrator).

3. (Optional) Start the OpenXR Developer Tools for Windows Mixed Reality, under the *System Status* tab, scroll down to *API Layers*. There should be no layer named `XR_APILAYER_NOVENDOR_nis_scaler`.

## App configuration

1. First, retrieve the name that the application passes to OpenXR. In order to do that, run the application while the API layer is enabled.

2. Locate the log file for the layer. It will typically be `%LocalAppData%\XR_APILAYER_NOVENDOR_nis_scaler.log`.

3. In the log file, search for the first line saying "Could not load config for ...":

```
dllHome is "C:\Program Files\OpenXR-API-Layers"
XR_APILAYER_NOVENDOR_nis_scaler layer is active
Could not load config for "FS2020"
Could not load config for "Zouna"
```

4. In the same folder where `XR_APILAYER_NOVENDOR_nis_scaler.json` was copied during setup, create a file with a name matching the application name, and with the extension `.cfg`. For example `C:\Program Files\OpenXR-API-Layers\FS2020.cfg`.

Example with scaling factor of 50% and sharpness of 80%:

```
scaling=0.5
sharpness=0.8
```

5. When running the application, the changes should take affect. Inspect the log file if it needs to be confirmed:

```
dllHome is "C:\Program Files\OpenXR-API-Layers"
XR_APILAYER_NOVENDOR_nis_scaler layer is active
Loading config for "FS2020"
```

## Keyboard shortcuts

Changing the sharpness can be done in increments of 5% by pressing Ctrl + Down arrow (or Ctrl + F2) to decrease and Ctrl + Up arrow (or Ctrl + F3) to increase. The new sharpness value can be observed in the log file (see README). When satisfied, you may then modify the config file to make it permanent.

You can use Ctrl + Left arrow (or Ctrl + F1) to enable/disable NIS and switch to a bilinear scaler (cheap scaler) instead, so you can see the improvements (hopefully) that NIS provides.

## Limitations

This OpenXR API layer is currently very limited in the mode and input it accepts:

* Only Direct3D 11 is supported;
* Not all swapchain formats are supported (exact list TBD);
* Cropped imageRect submission is not supported;
* Depth submission is not supported.

## Contributions

The author is Matthieu Bucchianeri (https://github.com/mbucchia/). Please note that this software is not affiliated with Microsoft.

Special thanks to BufordTX for spotting my bug in the installation script.
Special thanks to CptLucky8 for spotting an issue with the instructions.

Many thanks to the https://forums.flightsimulator.com/ community for the testing and feedback!
