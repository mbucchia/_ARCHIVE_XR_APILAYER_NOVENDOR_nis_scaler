using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Linq;
using System.Reflection;
using System.Security.Principal;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace ConfigUI
{
    static class Program
    {
        /// <summary>
        /// The main entry point for the application.
        /// </summary>
        [STAThread]
        static void Main()
        {
#if !DEBUG
            var principal = new WindowsPrincipal(WindowsIdentity.GetCurrent());
            if (!principal.IsInRole(WindowsBuiltInRole.Administrator))
            {
                var processInfo = new System.Diagnostics.ProcessStartInfo();
                processInfo.Verb = "RunAs";
                processInfo.FileName = Assembly.GetEntryAssembly().Location;
                try
                {
                    Process.Start(processInfo).WaitForExit();
                }
                catch (Win32Exception)
                {
                    MessageBox.Show("This application must be run as Administrator.", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
                Application.Exit();
                return;
            }
#endif
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new Form1());
        }
    }
}
