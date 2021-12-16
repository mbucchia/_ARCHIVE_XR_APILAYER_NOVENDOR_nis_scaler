using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Configuration;
using System.IO;

// Reference: https://www.c-sharpcorner.com/article/how-to-perform-custom-actions-and-upgrade-using-visual-studio-installer/
namespace SetupCustomActions
{
    [RunInstaller(true)]
    public partial class CustomActions : System.Configuration.Install.Installer
    {
        public CustomActions()
        {
        }

        protected override void OnAfterInstall(IDictionary savedState)
        {
            var installPath = Path.GetDirectoryName(Path.GetDirectoryName(base.Context.Parameters["AssemblyPath"]));
            var jsonName = "XR_APILAYER_NOVENDOR_nis_scaler.json";
            var jsonPath = installPath + "\\" + jsonName;

            // Delete any previously installed layer with our name.
            Microsoft.Win32.RegistryKey key;
            key = Microsoft.Win32.Registry.LocalMachine.CreateSubKey("SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit");
            var existingValues = key.GetValueNames();
            foreach (var value in existingValues)
            {
                if (value.EndsWith("\\" + jsonName))
                {
                    key.DeleteValue(value);
                }
            }

            // We want to add our layer at the very last position. In our case, this is easy, it just meand we need to create it!
            key.SetValue(jsonPath, 0);
            key.Close();

            base.OnAfterInstall(savedState);
        }

        [System.Security.Permissions.SecurityPermission(System.Security.Permissions.SecurityAction.Demand)]
        protected override void OnAfterUninstall(IDictionary savedState)
        {
            try
            {
                var installPath = Path.GetDirectoryName(Path.GetDirectoryName(base.Context.Parameters["AssemblyPath"]));
                var jsonPath = installPath + "\\XR_APILAYER_NOVENDOR_nis_scaler.json";

                // Delete our key.
                Microsoft.Win32.RegistryKey key;
                key = Microsoft.Win32.Registry.LocalMachine.OpenSubKey("SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit");
                key.DeleteValue(jsonPath);
                key.Close();
            }
            catch (Exception e)
            {
            }
            finally
            {
                base.OnAfterUninstall(savedState);
            }
        }
    }
}
