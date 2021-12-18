
namespace ConfigUI
{
    partial class Form1
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.components = new System.ComponentModel.Container();
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(Form1));
            this.applicationList = new System.Windows.Forms.ComboBox();
            this.scalingSlider = new System.Windows.Forms.TrackBar();
            this.enableNIS = new System.Windows.Forms.CheckBox();
            this.scalingLabel = new System.Windows.Forms.Label();
            this.sharpnessSlider = new System.Windows.Forms.TrackBar();
            this.label2 = new System.Windows.Forms.Label();
            this.label3 = new System.Windows.Forms.Label();
            this.deleteButton = new System.Windows.Forms.Button();
            this.sharpnessLabel = new System.Windows.Forms.Label();
            this.resolutionLabel = new System.Windows.Forms.Label();
            this.layerActive = new System.Windows.Forms.Label();
            this.tooltip = new System.Windows.Forms.ToolTip(this.components);
            this.refreshTimer = new System.Windows.Forms.Timer(this.components);
            this.label1 = new System.Windows.Forms.Label();
            this.reportIssuesLink = new System.Windows.Forms.LinkLabel();
            this.enableScreenshot = new System.Windows.Forms.CheckBox();
            ((System.ComponentModel.ISupportInitialize)(this.scalingSlider)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.sharpnessSlider)).BeginInit();
            this.SuspendLayout();
            // 
            // applicationList
            // 
            this.applicationList.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            this.applicationList.FormattingEnabled = true;
            this.applicationList.Items.AddRange(new object[] {
            "All other applications",
            "Add a new application..."});
            this.applicationList.Location = new System.Drawing.Point(25, 69);
            this.applicationList.Name = "applicationList";
            this.applicationList.Size = new System.Drawing.Size(348, 21);
            this.applicationList.TabIndex = 0;
            this.applicationList.SelectedIndexChanged += new System.EventHandler(this.applicationList_SelectedIndexChanged);
            // 
            // scalingSlider
            // 
            this.scalingSlider.Enabled = false;
            this.scalingSlider.Location = new System.Drawing.Point(47, 156);
            this.scalingSlider.Maximum = 100;
            this.scalingSlider.Minimum = 50;
            this.scalingSlider.Name = "scalingSlider";
            this.scalingSlider.Size = new System.Drawing.Size(222, 69);
            this.scalingSlider.TabIndex = 1;
            this.scalingSlider.TickFrequency = 5;
            this.scalingSlider.Value = 80;
            this.scalingSlider.Scroll += new System.EventHandler(this.scalingSlider_Scroll);
            // 
            // enableNIS
            // 
            this.enableNIS.AutoSize = true;
            this.enableNIS.Location = new System.Drawing.Point(25, 117);
            this.enableNIS.Name = "enableNIS";
            this.enableNIS.Size = new System.Drawing.Size(125, 21);
            this.enableNIS.TabIndex = 2;
            this.enableNIS.Text = "Enable NIS Scaling";
            this.enableNIS.UseVisualStyleBackColor = true;
            this.enableNIS.CheckedChanged += new System.EventHandler(this.enableNIS_CheckedChanged);
            // 
            // scalingLabel
            // 
            this.scalingLabel.AutoSize = true;
            this.scalingLabel.Location = new System.Drawing.Point(287, 156);
            this.scalingLabel.Name = "scalingLabel";
            this.scalingLabel.Size = new System.Drawing.Size(35, 13);
            this.scalingLabel.TabIndex = 3;
            this.scalingLabel.Text = "label1";
            // 
            // sharpnessSlider
            // 
            this.sharpnessSlider.Enabled = false;
            this.sharpnessSlider.Location = new System.Drawing.Point(47, 246);
            this.sharpnessSlider.Maximum = 100;
            this.sharpnessSlider.Name = "sharpnessSlider";
            this.sharpnessSlider.Size = new System.Drawing.Size(222, 69);
            this.sharpnessSlider.TabIndex = 4;
            this.sharpnessSlider.TickFrequency = 5;
            this.sharpnessSlider.Value = 50;
            this.sharpnessSlider.Scroll += new System.EventHandler(this.sharpnessSlider_Scroll);
            // 
            // label2
            // 
            this.label2.AutoSize = true;
            this.label2.Location = new System.Drawing.Point(50, 213);
            this.label2.Name = "label2";
            this.label2.Size = new System.Drawing.Size(57, 13);
            this.label2.TabIndex = 5;
            this.label2.Text = "Sharpness";
            // 
            // label3
            // 
            this.label3.AutoSize = true;
            this.label3.Location = new System.Drawing.Point(22, 49);
            this.label3.Name = "label3";
            this.label3.Size = new System.Drawing.Size(94, 13);
            this.label3.TabIndex = 6;
            this.label3.Text = "Select application:";
            // 
            // deleteButton
            // 
            this.deleteButton.Enabled = false;
            this.deleteButton.Location = new System.Drawing.Point(111, 339);
            this.deleteButton.Name = "deleteButton";
            this.deleteButton.Size = new System.Drawing.Size(191, 30);
            this.deleteButton.TabIndex = 7;
            this.deleteButton.Text = "Delete application profile";
            this.deleteButton.UseVisualStyleBackColor = true;
            this.deleteButton.Click += new System.EventHandler(this.deleteButton_Click);
            // 
            // sharpnessLabel
            // 
            this.sharpnessLabel.AutoSize = true;
            this.sharpnessLabel.Location = new System.Drawing.Point(287, 246);
            this.sharpnessLabel.Name = "sharpnessLabel";
            this.sharpnessLabel.Size = new System.Drawing.Size(35, 13);
            this.sharpnessLabel.TabIndex = 8;
            this.sharpnessLabel.Text = "label1";
            // 
            // resolutionLabel
            // 
            this.resolutionLabel.AutoSize = true;
            this.resolutionLabel.Location = new System.Drawing.Point(22, 18);
            this.resolutionLabel.Name = "resolutionLabel";
            this.resolutionLabel.Size = new System.Drawing.Size(102, 13);
            this.resolutionLabel.TabIndex = 9;
            this.resolutionLabel.Text = "OpenXR resolution: ";
            // 
            // layerActive
            // 
            this.layerActive.AutoSize = true;
            this.layerActive.Location = new System.Drawing.Point(251, 18);
            this.layerActive.Margin = new System.Windows.Forms.Padding(2, 0, 2, 0);
            this.layerActive.Name = "layerActive";
            this.layerActive.Size = new System.Drawing.Size(122, 13);
            this.layerActive.TabIndex = 10;
            this.layerActive.Text = "Layer state is not known";
            // 
            // refreshTimer
            // 
            this.refreshTimer.Interval = 10000;
            this.refreshTimer.Tick += new System.EventHandler(this.refreshTimer_Tick);
            // 
            // label1
            // 
            this.label1.AutoSize = true;
            this.label1.Font = new System.Drawing.Font("Microsoft Sans Serif", 9F, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.label1.Location = new System.Drawing.Point(20, 303);
            this.label1.Margin = new System.Windows.Forms.Padding(2, 0, 2, 0);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(524, 22);
            this.label1.TabIndex = 11;
            this.label1.Text = "Modifying settings require the VR session to be restarted.";
            // 
            // reportIssuesLink
            // 
            this.reportIssuesLink.AutoSize = true;
            this.reportIssuesLink.Location = new System.Drawing.Point(8, 422);
            this.reportIssuesLink.Margin = new System.Windows.Forms.Padding(2, 0, 2, 0);
            this.reportIssuesLink.Name = "reportIssuesLink";
            this.reportIssuesLink.Size = new System.Drawing.Size(71, 13);
            this.reportIssuesLink.TabIndex = 12;
            this.reportIssuesLink.TabStop = true;
            this.reportIssuesLink.Text = "Report issues";
            this.reportIssuesLink.LinkClicked += new System.Windows.Forms.LinkLabelLinkClickedEventHandler(this.reportIssuesLink_LinkClicked);
            // 
            // enableScreenshot
            // 
            this.enableScreenshot.AutoSize = true;
            this.enableScreenshot.Location = new System.Drawing.Point(25, 385);
            this.enableScreenshot.Name = "enableScreenshot";
            this.enableScreenshot.Size = new System.Drawing.Size(169, 21);
            this.enableScreenshot.TabIndex = 13;
            this.enableScreenshot.Text = "Enable screenshot (Ctrl+F12)";
            this.enableScreenshot.UseVisualStyleBackColor = true;
            this.enableScreenshot.CheckedChanged += new System.EventHandler(this.enableScreenshot_CheckedChanged);
            // 
            // Form1
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(411, 439);
            this.Controls.Add(this.enableScreenshot);
            this.Controls.Add(this.reportIssuesLink);
            this.Controls.Add(this.label1);
            this.Controls.Add(this.layerActive);
            this.Controls.Add(this.resolutionLabel);
            this.Controls.Add(this.sharpnessLabel);
            this.Controls.Add(this.deleteButton);
            this.Controls.Add(this.label3);
            this.Controls.Add(this.label2);
            this.Controls.Add(this.sharpnessSlider);
            this.Controls.Add(this.scalingLabel);
            this.Controls.Add(this.enableNIS);
            this.Controls.Add(this.scalingSlider);
            this.Controls.Add(this.applicationList);
            this.FormBorderStyle = System.Windows.Forms.FormBorderStyle.FixedSingle;
            this.Icon = ((System.Drawing.Icon)(resources.GetObject("$this.Icon")));
            this.MaximizeBox = false;
            this.Name = "Form1";
            this.Text = "OpenXR NIS Scaler configuration tool";
            ((System.ComponentModel.ISupportInitialize)(this.scalingSlider)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.sharpnessSlider)).EndInit();
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        private System.Windows.Forms.ComboBox applicationList;
        private System.Windows.Forms.TrackBar scalingSlider;
        private System.Windows.Forms.CheckBox enableNIS;
        private System.Windows.Forms.Label scalingLabel;
        private System.Windows.Forms.TrackBar sharpnessSlider;
        private System.Windows.Forms.Label label2;
        private System.Windows.Forms.Label label3;
        private System.Windows.Forms.Button deleteButton;
        private System.Windows.Forms.Label sharpnessLabel;
        private System.Windows.Forms.Label resolutionLabel;
        private System.Windows.Forms.Label layerActive;
        private System.Windows.Forms.ToolTip tooltip;
        private System.Windows.Forms.Timer refreshTimer;
        private System.Windows.Forms.Label label1;
        private System.Windows.Forms.LinkLabel reportIssuesLink;
        private System.Windows.Forms.CheckBox enableScreenshot;
    }
}

