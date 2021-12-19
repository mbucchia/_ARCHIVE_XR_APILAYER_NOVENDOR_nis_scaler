using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using Silk.NET.Core;
using Silk.NET.Core.Native;
using Silk.NET.OpenXR;

namespace ConfigUI
{
    public partial class Form1 : Form
    {
        // Must match dllmain.cpp.
        private const string RegPrefix = "SOFTWARE\\OpenXR_NIS_Scaler";
        private const int DefaultScaling = 80;
        private const int DefaultSharpness = 50;

        public Form1()
        {
            InitializeComponent();

            // TODO: Add popular applications here.
            applicationList.Items.Insert(applicationList.Items.Count - 1, "FS2020 -- Microsoft Flight Simulator 2020");
            protectedApplications = applicationList.Items.Count - 2;

            // Load any user-added application.
            try
            {
                Microsoft.Win32.RegistryKey reg = Microsoft.Win32.Registry.LocalMachine.CreateSubKey(RegPrefix);
                var applications = reg.GetSubKeyNames();
                foreach (var application in applications)
                {
                    // Make sure we don't re-add a preset application.
                    bool skip = false;
                    foreach (string existingApplication in applicationList.Items)
                    {
                        if (existingApplication.Split(new string[] { " -- " }, 2, StringSplitOptions.None)[0] == application)
                        {
                            skip = true;
                            break;
                        }
                    }

                    if (!skip)
                    {
                        applicationList.Items.Insert(applicationList.Items.Count - 1, application);
                    }
                }
            }
            catch (Exception)
            {
            }

            try
            {
                xr = XR.GetApi();
                InitXr();
                GetOpenXRResolution();
                refreshTimer.Enabled = true;
            }
            catch (Exception)
            {
                MessageBox.Show(this, "Failed to initialize OpenXR", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }

            // Initial state.
            applicationList.SelectedIndex = 0; // All
            applicationList_SelectedIndexChanged(null, null);
        }

        private XR xr = null;
        private uint resolutionWidth = 0;
        private uint resolutionHeight = 0;

        private int protectedApplications = 0;
        private string applicationKey = null;
        private bool loading = false;

        private unsafe void InitXr()
        {
            // Make sure our layer is installed.
            uint layerCount = 0;
            xr.EnumerateApiLayerProperties(ref layerCount, null);
            var layers = new ApiLayerProperties[layerCount];
            for (int i = 0; i < layers.Length; i++)
            {
                layers[i].Type = StructureType.TypeApiLayerProperties;
            }
            var layersSpan = new Span<ApiLayerProperties>(layers);
            if (xr.EnumerateApiLayerProperties(ref layerCount, layersSpan) == Result.Success)
            {
                bool found = false;
                string layersList = "";
                for (int i = 0; i < layers.Length; i++)
                {
                    fixed (void* nptr = layers[i].LayerName)
                    {
                        string layerName = SilkMarshal.PtrToString(new System.IntPtr(nptr));
                        layersList += layerName + "\n";
                        if (layerName == "XR_APILAYER_NOVENDOR_nis_scaler")
                        {
                            found = true;
                        }
                    }
                }

                tooltip.SetToolTip(layerActive, layersList);

                if (!found)
                {
                    layerActive.Text = "NIS Scaler layer is NOT active";
                    layerActive.ForeColor = Color.Red;
                    applicationList.Enabled = false;
                    enableNIS.Enabled = false;
                    enableScreenshot.Enabled = false;
                }
                else
                {
                    layerActive.Text = "NIS Scaler layer is active";
                    layerActive.ForeColor = Color.Green;
                    applicationList.Enabled = true;
                    enableNIS.Enabled = true;
                    enableScreenshot.Enabled = true;
                }
                enableNIS_CheckedChanged(null, null);
            }
            else
            {
                MessageBox.Show(this, "Failed to query API layers", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }

            // We don't want our layer to interfere. Disable it for this process.
            Environment.SetEnvironmentVariable("DISABLE_XR_APILAYER_NOVENDOR_nis_scaler", "1");
        }

        private unsafe void GetOpenXRResolution()
        {
            // Query the OpenXR resolution.
            Instance instance = new Instance(null);
            var createInfo = new InstanceCreateInfo(
                type: StructureType.TypeInstanceCreateInfo
            );
            createInfo.ApplicationInfo.ApiVersion = new Version64(1, 0, 0);
            var applicationName = new Span<byte>(createInfo.ApplicationInfo.ApplicationName, (int)XR.MaxApplicationNameSize);
            var engineName = new Span<byte>(createInfo.ApplicationInfo.EngineName, (int)XR.MaxEngineNameSize);
            SilkMarshal.StringIntoSpan("NIS-Scaler-Config-Tool", applicationName);
            SilkMarshal.StringIntoSpan("", engineName);
            if (xr.CreateInstance(createInfo, ref instance) == Result.Success)
            {
                try
                {
                    ulong systemId = 0;
                    var getInfo = new SystemGetInfo(
                        type: StructureType.TypeSystemGetInfo,
                        formFactor: FormFactor.HeadMountedDisplay
                    );

                    if (xr.GetSystem(instance, getInfo, ref systemId) == Result.Success)
                    {
                        uint viewCount = 0;
                        xr.EnumerateViewConfigurationView(instance, systemId, ViewConfigurationType.PrimaryStereo, ref viewCount, null);
                        var views = new ViewConfigurationView[viewCount];
                        for (int i = 0; i < views.Length; i++)
                        {
                            views[i].Type = StructureType.TypeViewConfigurationView;
                        }
                        var viewsSpan = new Span<ViewConfigurationView>(views);
                        if (xr.EnumerateViewConfigurationView(instance, systemId, ViewConfigurationType.PrimaryStereo, ref viewCount, views) == Result.Success)
                        {
                            resolutionWidth = views[0].RecommendedImageRectWidth;
                            resolutionHeight = views[0].RecommendedImageRectHeight;

                            resolutionLabel.Text = "OpenXR resolution: " + resolutionWidth + " x " + resolutionHeight;
                            scalingSlider_Scroll(null, null);
                        }
                    }
                    else
                    {
                        resolutionLabel.Text = "OpenXR resolution: Please turn on headset";
                    }
                }
                finally
                {
                    xr.DestroyInstance(instance);
                }
            }
        }

        private void LoadSettings(string key)
        {
            Microsoft.Win32.RegistryKey reg = null;
            try
            {
                loading = true;
                {
                    reg = Microsoft.Win32.Registry.LocalMachine.CreateSubKey(RegPrefix + (key == null ? "" : "\\" + key));
                    enableNIS.Checked = (int)reg.GetValue("enabled", 0) == 1 ? true : false;
                    scalingSlider.Value = (int)reg.GetValue("scaling", DefaultScaling);
                    sharpnessSlider.Value = (int)reg.GetValue("sharpness", DefaultSharpness);
                    reg.Close();
                }
                {
                    reg = Microsoft.Win32.Registry.LocalMachine.CreateSubKey(RegPrefix);
                    enableScreenshot.Checked = (int)reg.GetValue("enable_screenshots", 0) == 1 ? true : false;
                }
            }
            catch (Exception exc)
            {
                MessageBox.Show(this, "Error loading settings: " + exc.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                loading = false;
                if (reg != null)
                {
                    reg.Close();
                }
            }
        }

        private void SaveSettings(string key)
        {
            if (loading)
            {
                return;
            }
            Microsoft.Win32.RegistryKey reg = null;
            try
            {
                {
                    reg = Microsoft.Win32.Registry.LocalMachine.CreateSubKey(RegPrefix + (key == null ? "" : "\\" + key));
                    reg.SetValue("enabled", enableNIS.Checked ? 1 : 0, Microsoft.Win32.RegistryValueKind.DWord);
                    reg.SetValue("scaling", scalingSlider.Value, Microsoft.Win32.RegistryValueKind.DWord);
                    reg.SetValue("sharpness", sharpnessSlider.Value, Microsoft.Win32.RegistryValueKind.DWord);
                    reg.Close();
                }
                {
                    reg = Microsoft.Win32.Registry.LocalMachine.CreateSubKey(RegPrefix);
                    reg.SetValue("enable_screenshots", enableScreenshot.Checked ? 1 : 0, Microsoft.Win32.RegistryValueKind.DWord);
                }
            }
            catch (Exception exc)
            {
                MessageBox.Show(this, "Error updating settings: " + exc.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                if (reg != null)
                {
                    reg.Close();
                }
            }
        }

        private void DeleteSettings(string key)
        {
            if (key == null)
            {
                return;
            }

            try
            {
                Microsoft.Win32.Registry.LocalMachine.DeleteSubKeyTree(RegPrefix + "\\" + key);
            }
            catch (Exception)
            {
            }
        }

        private void applicationList_SelectedIndexChanged(object sender, EventArgs e)
        {
            if (applicationList.SelectedIndex == applicationList.Items.Count - 1)
            {
                // Add a new entry.

                MessageBox.Show(this, "IMPORTANT: The name to enter at the following prompt is the name that the application passes to OpenXR. " +
                    "This name is very likely not identical to the name of the program/shortcut. " +
                    "Please take a look at the %LocalAppData%\\XR_APILAYER_NOVENDOR_nis_scaler.log file after starting up the application.",
                    "Important", MessageBoxButtons.OK, MessageBoxIcon.Exclamation);

                string applicationName = "";
                if (ShowInputDialog("Name", ref applicationName) == DialogResult.OK && applicationName != "")
                {
                    if (!applicationList.Items.Contains(applicationName))
                    {
                        applicationList.Items.Insert(applicationList.Items.Count - 1, applicationName);
                        applicationList.SelectedIndex = applicationList.Items.Count - 2;
                    }
                    else
                    {
                        applicationList.SelectedIndex = applicationList.Items.IndexOf(applicationName);
                    }
                    deleteButton.Enabled = true;
                }
                else
                {
                    applicationList.SelectedIndex = 0;
                }
                applicationList_SelectedIndexChanged(null, null);
            }
            else if (applicationList.SelectedIndex > 0)
            {
                // Selected application name.
                deleteButton.Enabled = applicationList.SelectedIndex > protectedApplications;
                applicationKey = applicationList.SelectedItem.ToString().Split(new string[] { " -- " }, 2, StringSplitOptions.None)[0];
                LoadSettings(applicationKey);
            }
            else
            {
                // All applications.
                deleteButton.Enabled = false;
                applicationKey = null;
                LoadSettings(null);
            }

            enableNIS_CheckedChanged(null, null);
            scalingSlider_Scroll(null, null);
            sharpnessSlider_Scroll(null, null);
        }

        private void enableNIS_CheckedChanged(object sender, EventArgs e)
        {
            if (sender != null)
            {
                SaveSettings(applicationKey);
            }

            scalingSlider.Enabled = enableNIS.Enabled && enableNIS.Checked;
            sharpnessSlider.Enabled = enableNIS.Enabled && enableNIS.Checked;
        }

        private void scalingSlider_Scroll(object sender, EventArgs e)
        {
            if (sender != null)
            {
                SaveSettings(applicationKey);
            }

            var scaledWidth = (resolutionWidth * scalingSlider.Value) / 100;
            var scaledHeight = (resolutionHeight * scalingSlider.Value) / 100;
            scalingLabel.Text = scalingSlider.Value + "%";
            if (resolutionWidth != 0)
            {
                scalingLabel.Text += "\n" + scaledWidth + " x " + scaledHeight;
            }
        }

        private void sharpnessSlider_Scroll(object sender, EventArgs e)
        {
            if (sender != null)
            {
                SaveSettings(applicationKey);
            }

            sharpnessLabel.Text = sharpnessSlider.Value + "%";
        }

        private void deleteButton_Click(object sender, EventArgs e)
        {
            DeleteSettings(applicationKey);

            applicationList.Items.Remove(applicationList.SelectedItem);

            applicationList.SelectedIndex = 0;
            applicationList_SelectedIndexChanged(null, null);
        }

        private void enableScreenshot_CheckedChanged(object sender, EventArgs e)
        {
            if (sender != null)
            {
                SaveSettings(applicationKey);
            }
        }

        private void refreshTimer_Tick(object sender, EventArgs e)
        {
            GetOpenXRResolution();
        }

        private void reportIssuesLink_LinkClicked(object sender, LinkLabelLinkClickedEventArgs e)
        {
            string githubIssues = "https://github.com/mbucchia/XR_APILAYER_NOVENDOR_nis_scaler/issues";

            reportIssuesLink.LinkVisited = true;
            System.Diagnostics.Process.Start(githubIssues);
        }

        // https://stackoverflow.com/questions/97097/what-is-the-c-sharp-version-of-vb-nets-inputdialog
        private static DialogResult ShowInputDialog(string prompt, ref string input)
        {
            System.Drawing.Size size = new System.Drawing.Size(200, 70);
            Form inputBox = new Form();

            inputBox.FormBorderStyle = System.Windows.Forms.FormBorderStyle.FixedDialog;
            inputBox.ClientSize = size;
            inputBox.Text = prompt;

            System.Windows.Forms.TextBox textBox = new TextBox();
            textBox.Size = new System.Drawing.Size(size.Width - 10, 23);
            textBox.Location = new System.Drawing.Point(5, 5);
            textBox.Text = input;
            inputBox.Controls.Add(textBox);

            Button okButton = new Button();
            okButton.DialogResult = System.Windows.Forms.DialogResult.OK;
            okButton.Name = "okButton";
            okButton.Size = new System.Drawing.Size(75, 23);
            okButton.Text = "&OK";
            okButton.Location = new System.Drawing.Point(size.Width - 80 - 80, 39);
            inputBox.Controls.Add(okButton);

            Button cancelButton = new Button();
            cancelButton.DialogResult = System.Windows.Forms.DialogResult.Cancel;
            cancelButton.Name = "cancelButton";
            cancelButton.Size = new System.Drawing.Size(75, 23);
            cancelButton.Text = "&Cancel";
            cancelButton.Location = new System.Drawing.Point(size.Width - 80, 39);
            inputBox.Controls.Add(cancelButton);

            inputBox.AcceptButton = okButton;
            inputBox.CancelButton = cancelButton;

            DialogResult result = inputBox.ShowDialog();
            input = textBox.Text;
            return result;
        }
    }
}
