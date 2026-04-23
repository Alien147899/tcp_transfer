using System.Diagnostics;
using System.IO.Compression;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;

namespace LanTransfer.Gui;

public sealed class MainForm : Form
{
    private readonly string _projectRoot;
    private readonly string _exePath;
    private readonly object _stdinLock = new();

    private Process? _receiverProcess;
    private ushort? _activeReceiverPort;
    private string? _activeDownloadFolder;
    private string? _selectedPairedDeviceId;
    private readonly List<PairedDeviceRecord> _pairedDeviceRecords = new();

    private Label _localDeviceLabel = null!;
    private Label _receiverStatusLabel = null!;
    private Label _connectionCodeLabel = null!;
    private TextBox _connectionCodeInputTextBox = null!;
    private TextBox _portTextBox = null!;
    private TextBox _downloadTextBox = null!;
    private ComboBox _sendTargetComboBox = null!;
    private TextBox _sendFileTextBox = null!;
    private Label _sendSelectionLabel = null!;
    private ListView _pairedDevicesListView = null!;
    private TextBox _logTextBox = null!;
    private Button _startReceiverButton = null!;
    private Button _stopReceiverButton = null!;
    private Button _discoverButton = null!;
    private Button _browseButton = null!;
    private Button _browseSendFileButton = null!;
    private Button _browseSendFolderButton = null!;
    private Button _pairByCodeButton = null!;
    private Button _openDownloadsButton = null!;
    private Button _openProjectButton = null!;
    private Button _removePairedDeviceButton = null!;
    private Button _sendFileButton = null!;
    private System.Windows.Forms.Timer _statusTimer = null!;
    private SendPayloadKind _sendPayloadKind = SendPayloadKind.None;

    public MainForm()
    {
        (_projectRoot, _exePath) = ResolveAppPaths();

        InitializeUi();
        RefreshStatus();
    }

    private void InitializeUi()
    {
        Text = "LAN Transfer Desktop";
        StartPosition = FormStartPosition.CenterScreen;
        Width = 1160;
        Height = 860;
        MinimumSize = new Size(1040, 760);
        BackColor = Color.FromArgb(247, 242, 233);

        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 5,
            Padding = new Padding(20, 18, 20, 18),
            BackColor = BackColor,
        };
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 270));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 220));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 58));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 42));
        Controls.Add(root);

        var header = new TableLayoutPanel
        {
            Dock = DockStyle.Top,
            AutoSize = true,
            ColumnCount = 1,
            RowCount = 5,
            Margin = new Padding(0, 0, 0, 14),
        };
        header.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        header.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        header.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        header.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        header.RowStyles.Add(new RowStyle(SizeType.AutoSize));

        header.Controls.Add(new Label
        {
            Text = "LAN Transfer Desktop",
            Font = new Font("Segoe UI", 24, FontStyle.Bold),
            AutoSize = true,
            Margin = new Padding(0, 0, 0, 6),
        });

        header.Controls.Add(new Label
        {
            Text = "Start the receiver, share a short connection code, and manage trusted paired devices.",
            Font = new Font("Segoe UI", 11),
            AutoSize = true,
            Margin = new Padding(0, 0, 0, 12),
        });

        _localDeviceLabel = new Label
        {
            AutoSize = true,
            Font = new Font("Consolas", 10.5f),
            Margin = new Padding(0, 0, 0, 4),
        };
        header.Controls.Add(_localDeviceLabel);

        _receiverStatusLabel = new Label
        {
            AutoSize = true,
            Font = new Font("Segoe UI", 11, FontStyle.Bold),
            Margin = new Padding(0, 0, 0, 4),
        };
        header.Controls.Add(_receiverStatusLabel);

        _connectionCodeLabel = new Label
        {
            AutoSize = true,
            Font = new Font("Consolas", 15, FontStyle.Bold),
            Margin = new Padding(0),
        };
        header.Controls.Add(_connectionCodeLabel);

        var codeActionRow = new FlowLayoutPanel
        {
            Dock = DockStyle.Top,
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false,
            Margin = new Padding(0, 12, 0, 0),
        };
        header.Controls.Add(codeActionRow);

        codeActionRow.Controls.Add(new Label
        {
            Text = "Enter code",
            AutoSize = true,
            Margin = new Padding(0, 8, 10, 0),
        });

        _connectionCodeInputTextBox = new TextBox
        {
            Width = 120,
            Margin = new Padding(0, 2, 10, 0),
            MaxLength = 6,
        };
        codeActionRow.Controls.Add(_connectionCodeInputTextBox);

        _pairByCodeButton = new Button
        {
            Text = "Pair By Code",
            Size = new Size(120, 34),
            Margin = new Padding(0),
        };
        _pairByCodeButton.Click += async (_, _) => await PairByCodeAsync();
        codeActionRow.Controls.Add(_pairByCodeButton);

        root.Controls.Add(header, 0, 0);

        var receiverGroup = new GroupBox
        {
            Text = "Receiver",
            Dock = DockStyle.Fill,
            MinimumSize = new Size(0, 270),
            Margin = new Padding(0, 0, 0, 14),
        };
        root.Controls.Add(receiverGroup, 0, 1);

        var receiverLayout = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 5,
            RowCount = 3,
            Padding = new Padding(12, 14, 12, 12),
        };
        receiverLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 120));
        receiverLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        receiverLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 110));
        receiverLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 150));
        receiverLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 150));
        receiverLayout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        receiverLayout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        receiverLayout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        receiverGroup.Controls.Add(receiverLayout);

        receiverLayout.Controls.Add(new Label
        {
            Text = "Listen Port",
            AutoSize = true,
            Anchor = AnchorStyles.Left,
            Margin = new Padding(0, 0, 10, 6),
        }, 0, 0);

        receiverLayout.Controls.Add(new Label
        {
            Text = "Download Folder",
            AutoSize = true,
            Anchor = AnchorStyles.Left,
            Margin = new Padding(0, 0, 10, 6),
        }, 1, 0);

        _portTextBox = new TextBox
        {
            Text = "9000",
            Dock = DockStyle.Fill,
            Margin = new Padding(0, 0, 10, 12),
        };
        receiverLayout.Controls.Add(_portTextBox, 0, 1);

        _downloadTextBox = new TextBox
        {
            Text = @".\downloads",
            Dock = DockStyle.Fill,
            Margin = new Padding(0, 0, 10, 12),
        };
        receiverLayout.Controls.Add(_downloadTextBox, 1, 1);

        _browseButton = new Button
        {
            Text = "Browse...",
            Dock = DockStyle.Fill,
            Height = 34,
            Margin = new Padding(0, 0, 10, 12),
        };
        _browseButton.Click += (_, _) => BrowseDownloadFolder();
        receiverLayout.Controls.Add(_browseButton, 3, 1);

        _openProjectButton = new Button
        {
            Text = "Open Project",
            Dock = DockStyle.Fill,
            Height = 34,
            Margin = new Padding(0, 0, 0, 12),
        };
        _openProjectButton.Click += (_, _) => Process.Start("explorer.exe", _projectRoot);
        receiverLayout.Controls.Add(_openProjectButton, 4, 1);

        var actionBar = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false,
            Margin = new Padding(0),
        };

        _startReceiverButton = BuildActionButton("Start Receiver", StartReceiver);
        _stopReceiverButton = BuildActionButton("Stop Receiver", StopReceiver);
        _discoverButton = BuildActionButton("Discover Devices", async () => await DiscoverDevicesAsync());
        _openDownloadsButton = BuildActionButton("Open Downloads", OpenDownloads);

        actionBar.Controls.Add(_startReceiverButton);
        actionBar.Controls.Add(_stopReceiverButton);
        actionBar.Controls.Add(_discoverButton);
        actionBar.Controls.Add(_openDownloadsButton);

        receiverLayout.SetColumnSpan(actionBar, 5);
        receiverLayout.Controls.Add(actionBar, 0, 2);

        var sendGroup = new GroupBox
        {
            Text = "Send File",
            Dock = DockStyle.Fill,
            MinimumSize = new Size(0, 220),
            Margin = new Padding(0, 0, 0, 14),
        };
        root.Controls.Add(sendGroup, 0, 2);

        var sendLayout = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 4,
            RowCount = 4,
            Padding = new Padding(12, 14, 12, 12),
        };
        sendLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 170));
        sendLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        sendLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 130));
        sendLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 240));
        sendLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 56));
        sendLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 54));
        sendLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 34));
        sendLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 44));
        sendGroup.Controls.Add(sendLayout);

        sendLayout.Controls.Add(new Label
        {
            Text = "Target Paired Device",
            AutoSize = true,
            Anchor = AnchorStyles.Left,
            Margin = new Padding(0, 0, 10, 6),
        }, 0, 0);
        sendLayout.Controls.Add(new Label
        {
            Text = "File Path",
            AutoSize = true,
            Anchor = AnchorStyles.Left,
            Margin = new Padding(0, 0, 10, 6),
        }, 1, 0);

        _sendTargetComboBox = new ComboBox
        {
            Dock = DockStyle.Fill,
            DropDownStyle = ComboBoxStyle.DropDownList,
            Margin = new Padding(0, 0, 10, 12),
        };
        _sendTargetComboBox.SelectedIndexChanged += (_, _) => SyncSelectedPairedDeviceFromCombo();
        sendLayout.Controls.Add(_sendTargetComboBox, 0, 1);

        _sendFileTextBox = new TextBox
        {
            Dock = DockStyle.Fill,
            Margin = new Padding(0, 0, 10, 12),
        };
        sendLayout.Controls.Add(_sendFileTextBox, 1, 1);

        _browseSendFileButton = new Button
        {
            Text = "Choose File",
            Dock = DockStyle.Fill,
            Height = 34,
            Margin = new Padding(0, 0, 10, 12),
        };
        _browseSendFileButton.Click += (_, _) => BrowseSendFile();
        sendLayout.Controls.Add(_browseSendFileButton, 2, 1);

        var sendActions = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false,
            Margin = new Padding(0, 0, 0, 12),
        };
        sendLayout.Controls.Add(sendActions, 3, 1);

        _browseSendFolderButton = new Button
        {
            Text = "Folder...",
            Size = new Size(118, 34),
            Margin = new Padding(0, 0, 8, 0),
        };
        _browseSendFolderButton.Click += (_, _) => BrowseSendFolder();
        sendActions.Controls.Add(_browseSendFolderButton);

        _sendFileButton = new Button
        {
            Text = "Send",
            Size = new Size(86, 34),
            Margin = new Padding(0),
        };
        _sendFileButton.Click += async (_, _) => await SendFileAsync();
        sendActions.Controls.Add(_sendFileButton);

        _sendSelectionLabel = new Label
        {
            Text = "Current selection: no file or folder chosen.",
            AutoSize = true,
            Margin = new Padding(0, 0, 0, 4),
        };
        sendLayout.SetColumnSpan(_sendSelectionLabel, 4);
        sendLayout.Controls.Add(_sendSelectionLabel, 0, 2);

        var sendHint = new Label
        {
            Text = "Only paired devices with a stored host and port can be used as send targets. Folders are zipped before sending.",
            AutoSize = true,
            Margin = new Padding(0),
        };
        sendLayout.SetColumnSpan(sendHint, 4);
        sendLayout.Controls.Add(sendHint, 0, 3);

        var pairedGroup = new GroupBox
        {
            Text = "Trusted Paired Devices",
            Dock = DockStyle.Fill,
            Margin = new Padding(0, 0, 0, 14),
        };
        root.Controls.Add(pairedGroup, 0, 3);

        var pairedLayout = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 3,
            Padding = new Padding(12, 14, 12, 12),
        };
        pairedLayout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        pairedLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        pairedLayout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        pairedGroup.Controls.Add(pairedLayout);

        pairedLayout.Controls.Add(new Label
        {
            Text = "Paired devices are trusted records. They remain here even when the other device is offline, until you remove them.",
            AutoSize = true,
            Margin = new Padding(0, 0, 0, 10),
        }, 0, 0);

        _pairedDevicesListView = new ListView
        {
            Dock = DockStyle.Fill,
            View = View.Details,
            FullRowSelect = true,
            GridLines = true,
            HideSelection = false,
            MultiSelect = false,
            Margin = new Padding(0, 0, 0, 10),
        };
        _pairedDevicesListView.Columns.Add("Device ID", 430);
        _pairedDevicesListView.Columns.Add("Last Known Host", 260);
        _pairedDevicesListView.Columns.Add("Port", 90);
        _pairedDevicesListView.Columns.Add("Meaning", 180);
        _pairedDevicesListView.SelectedIndexChanged += (_, _) => UpdatePairedDeviceActions();
        pairedLayout.Controls.Add(_pairedDevicesListView, 0, 1);

        var pairedActions = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false,
            Margin = new Padding(0),
        };

        _removePairedDeviceButton = BuildActionButton("Remove Selected", RemoveSelectedPairedDevice);
        _removePairedDeviceButton.Enabled = false;
        pairedActions.Controls.Add(_removePairedDeviceButton);
        pairedLayout.Controls.Add(pairedActions, 0, 2);

        var logGroup = new GroupBox
        {
            Text = "Log",
            Dock = DockStyle.Fill,
        };
        root.Controls.Add(logGroup, 0, 4);

        _logTextBox = new TextBox
        {
            Dock = DockStyle.Fill,
            Multiline = true,
            ReadOnly = true,
            ScrollBars = ScrollBars.Vertical,
            Font = new Font("Consolas", 10),
            Margin = new Padding(12, 14, 12, 12),
        };
        logGroup.Controls.Add(_logTextBox);

        _statusTimer = new System.Windows.Forms.Timer { Interval = 1000 };
        _statusTimer.Tick += (_, _) => RefreshStatus();
        _statusTimer.Start();

        FormClosing += (_, _) => StopReceiver();
    }

    private Button BuildActionButton(string text, Action onClick)
    {
        var button = new Button
        {
            Text = text,
            Size = new Size(150, 38),
            Margin = new Padding(0, 0, 10, 0),
        };
        button.Click += (_, _) => onClick();
        return button;
    }

    private Button BuildActionButton(string text, Func<Task> onClick)
    {
        var button = new Button
        {
            Text = text,
            Size = new Size(150, 38),
            Margin = new Padding(0, 0, 10, 0),
        };
        button.Click += async (_, _) => await onClick();
        return button;
    }

    private void RefreshStatus()
    {
        var deviceInfoPath = Path.Combine(_projectRoot, "device_info.json");
        if (File.Exists(deviceInfoPath))
        {
            try
            {
                var deviceJson = JsonDocument.Parse(File.ReadAllText(deviceInfoPath));
                var root = deviceJson.RootElement;
                var deviceName = root.TryGetProperty("device_name", out var nameValue) ? nameValue.GetString() : "Unknown";
                var deviceId = root.TryGetProperty("device_id", out var idValue) ? idValue.GetString() : "Unknown";
                _localDeviceLabel.Text = $"Local device: {deviceName} [{deviceId}]";
                _connectionCodeLabel.Text = $"Connection code: {MakeConnectionCode(deviceId ?? string.Empty)}";
            }
            catch
            {
                _localDeviceLabel.Text = "Local device: failed to read device_info.json";
                _connectionCodeLabel.Text = "Connection code: unavailable";
            }
        }
        else
        {
            _localDeviceLabel.Text = "Local device: not initialized";
            _connectionCodeLabel.Text = "Connection code: starts after receiver initializes";
        }

        if (_receiverProcess is { HasExited: false })
        {
            var activePort = _activeReceiverPort?.ToString() ?? "unknown";
            var activeFolder = string.IsNullOrWhiteSpace(_activeDownloadFolder) ? "unknown" : _activeDownloadFolder;
            _receiverStatusLabel.Text = $"Receiver status: running on {activePort}, saving to {activeFolder}";
            _receiverStatusLabel.ForeColor = Color.FromArgb(16, 112, 62);
        }
        else
        {
            _receiverStatusLabel.Text = "Receiver status: stopped";
            _receiverStatusLabel.ForeColor = Color.FromArgb(180, 40, 40);
            _activeReceiverPort = null;
            _activeDownloadFolder = null;
        }

        RefreshPairedDevices();
    }

    private void RefreshPairedDevices()
    {
        var previouslySelectedId = _selectedPairedDeviceId;
        if (_pairedDevicesListView.SelectedItems.Count > 0)
        {
            previouslySelectedId = _pairedDevicesListView.SelectedItems[0].Tag as string ?? previouslySelectedId;
        }
        var previousSendTargetId = (_sendTargetComboBox.SelectedItem as SendTargetItem)?.DeviceId;

        _pairedDevicesListView.BeginUpdate();
        try
        {
            _pairedDevicesListView.Items.Clear();
            _sendTargetComboBox.BeginUpdate();
            _sendTargetComboBox.Items.Clear();
            _pairedDeviceRecords.Clear();
            _selectedPairedDeviceId = previouslySelectedId;

            var pairedDevicesPath = Path.Combine(_projectRoot, "paired_devices.json");
            if (!File.Exists(pairedDevicesPath))
            {
                UpdatePairedDeviceActions();
                return;
            }

            using var doc = JsonDocument.Parse(File.ReadAllText(pairedDevicesPath));
            if (!doc.RootElement.TryGetProperty("devices", out var devices) || devices.ValueKind != JsonValueKind.Array)
            {
                UpdatePairedDeviceActions();
                return;
            }

            foreach (var device in devices.EnumerateArray())
            {
                var id = device.TryGetProperty("device_id", out var idValue) ? idValue.GetString() ?? "?" : "?";
                var host = device.TryGetProperty("host", out var hostValue) ? hostValue.GetString() ?? "" : "";
                var port = device.TryGetProperty("port", out var portValue) ? portValue.ToString() : "";
                var meaning = string.IsNullOrWhiteSpace(host) || port == "0"
                    ? "paired only"
                    : "last known route";
                var numericPort = int.TryParse(port, out var parsedPort) ? parsedPort : 0;
                _pairedDeviceRecords.Add(new PairedDeviceRecord(id, host, numericPort));

                var item = new ListViewItem(id);
                item.SubItems.Add(string.IsNullOrWhiteSpace(host) ? "(not stored)" : host);
                item.SubItems.Add(port == "0" ? "-" : port);
                item.SubItems.Add(meaning);
                item.Tag = id;
                _pairedDevicesListView.Items.Add(item);

                if (!string.IsNullOrWhiteSpace(previouslySelectedId) && id == previouslySelectedId)
                {
                    item.Selected = true;
                }

                if (!string.IsNullOrWhiteSpace(host) && numericPort > 0)
                {
                    var comboItem = new SendTargetItem(id, host, numericPort);
                    _sendTargetComboBox.Items.Add(comboItem);
                    if ((previousSendTargetId != null && comboItem.DeviceId == previousSendTargetId) ||
                        (previousSendTargetId == null && id == previouslySelectedId))
                    {
                        _sendTargetComboBox.SelectedItem = comboItem;
                    }
                }
            }
        }
        catch
        {
            _pairedDevicesListView.Items.Clear();
            _pairedDevicesListView.Items.Add(new ListViewItem(new[]
            {
                "failed to read paired_devices.json",
                "",
                "",
                "",
            }));
        }
        finally
        {
            _sendTargetComboBox.EndUpdate();
            if (_sendTargetComboBox.SelectedIndex < 0 && _sendTargetComboBox.Items.Count > 0)
            {
                _sendTargetComboBox.SelectedIndex = 0;
            }
            _pairedDevicesListView.EndUpdate();
            UpdatePairedDeviceActions();
            _sendFileButton.Enabled = _sendTargetComboBox.Items.Count > 0;
        }
    }

    private void UpdatePairedDeviceActions()
    {
        if (_pairedDevicesListView.SelectedItems.Count == 0)
        {
            _removePairedDeviceButton.Enabled = !string.IsNullOrWhiteSpace(_selectedPairedDeviceId);
            return;
        }

        _selectedPairedDeviceId = _pairedDevicesListView.SelectedItems[0].Tag as string;
        _removePairedDeviceButton.Enabled = !string.IsNullOrWhiteSpace(_selectedPairedDeviceId);
        SyncSendTargetToSelectedPairedDevice();
    }

    private void SyncSendTargetToSelectedPairedDevice()
    {
        if (string.IsNullOrWhiteSpace(_selectedPairedDeviceId))
        {
            return;
        }

        for (var index = 0; index < _sendTargetComboBox.Items.Count; index++)
        {
            if (_sendTargetComboBox.Items[index] is SendTargetItem item && item.DeviceId == _selectedPairedDeviceId)
            {
                _sendTargetComboBox.SelectedIndex = index;
                return;
            }
        }
    }

    private void SyncSelectedPairedDeviceFromCombo()
    {
        if (_sendTargetComboBox.SelectedItem is SendTargetItem item)
        {
            _selectedPairedDeviceId = item.DeviceId;
            foreach (ListViewItem listItem in _pairedDevicesListView.Items)
            {
                if (!string.Equals(listItem.Tag as string, item.DeviceId, StringComparison.Ordinal))
                {
                    continue;
                }

                listItem.Selected = true;
                listItem.Focused = true;
                listItem.EnsureVisible();
                return;
            }
        }
    }

    private void BrowseDownloadFolder()
    {
        using var dialog = new FolderBrowserDialog();
        var current = ResolveDownloadFolder();
        if (Directory.Exists(current))
        {
            dialog.SelectedPath = current;
        }
        if (dialog.ShowDialog(this) == DialogResult.OK)
        {
            _downloadTextBox.Text = dialog.SelectedPath;
        }
    }

    private async Task PairByCodeAsync()
    {
        var code = _connectionCodeInputTextBox.Text.Trim();
        if (code.Length != 6 || !code.All(char.IsDigit))
        {
            ShowError("Enter a 6-digit connection code.");
            return;
        }

        _pairByCodeButton.Enabled = false;
        try
        {
            AppendLog($"Resolving connection code {code} ...");
            var match = await Task.Run(() => ResolveConnectionCode(code));
            if (match is null)
            {
                ShowError($"No device responded to connection code {code}.");
                return;
            }

            AppendLog($"Code {code} matched {match.DeviceName} [{match.DeviceId}] at {match.Host}:{match.Port}");
            var output = await Task.Run(() =>
            {
                var psi = new ProcessStartInfo
                {
                    FileName = _exePath,
                    WorkingDirectory = _projectRoot,
                    Arguments = $"pair {match.Host} {match.Port}",
                    RedirectStandardInput = true,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                };
                using var process = Process.Start(psi)!;
                process.StandardInput.WriteLine("y");
                process.StandardInput.WriteLine("y");
                process.StandardInput.Flush();
                process.StandardInput.Close();
                var stdout = process.StandardOutput.ReadToEnd();
                var stderr = process.StandardError.ReadToEnd();
                process.WaitForExit();
                if (process.ExitCode != 0)
                {
                    throw new InvalidOperationException(string.IsNullOrWhiteSpace(stderr) ? stdout : stderr);
                }
                return string.IsNullOrWhiteSpace(stdout) ? "Pairing completed successfully." : stdout.Trim();
            });

            AppendLog(output);
            RefreshStatus();
            ShowInfo(output);
        }
        catch (Exception ex)
        {
            ShowError($"Pair by code failed: {ex.Message}");
        }
        finally
        {
            _pairByCodeButton.Enabled = true;
        }
    }

    private string ResolveDownloadFolder()
    {
        var raw = _downloadTextBox.Text.Trim();
        if (string.IsNullOrWhiteSpace(raw))
        {
            raw = @".\downloads";
        }
        return Path.GetFullPath(Path.Combine(_projectRoot, raw));
    }

    private void StartReceiver()
    {
        if (!File.Exists(_exePath))
        {
            ShowError($"Desktop executable not found:\n{_exePath}");
            return;
        }
        if (_receiverProcess is { HasExited: false })
        {
            ShowInfo("Receiver is already running.");
            return;
        }

        if (!ushort.TryParse(_portTextBox.Text.Trim(), out var port))
        {
            ShowError("Listen port is invalid.");
            return;
        }

        var downloadFolder = ResolveDownloadFolder();
        Directory.CreateDirectory(downloadFolder);

        if (_receiverProcess is { HasExited: false })
        {
            var samePort = _activeReceiverPort == port;
            var sameFolder = string.Equals(_activeDownloadFolder, downloadFolder, StringComparison.OrdinalIgnoreCase);
            if (samePort && sameFolder)
            {
                ShowInfo("Receiver is already running.");
                return;
            }

            AppendLog($"Receiver settings changed. Restarting on port {port}, folder {downloadFolder}");
            StopReceiver();
        }

        var startInfo = new ProcessStartInfo
        {
            FileName = _exePath,
            WorkingDirectory = _projectRoot,
            Arguments = $"receive {port} \"{downloadFolder}\"",
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8,
        };

        _receiverProcess = new Process { StartInfo = startInfo, EnableRaisingEvents = true };
        _receiverProcess.OutputDataReceived += ReceiverOutputDataReceived;
        _receiverProcess.ErrorDataReceived += ReceiverErrorDataReceived;
        _receiverProcess.Exited += (_, _) =>
        {
            BeginInvoke(() =>
            {
                AppendLog("Receiver process exited.");
                RefreshStatus();
            });
        };

        _receiverProcess.Start();
        _receiverProcess.BeginOutputReadLine();
        _receiverProcess.BeginErrorReadLine();
        _activeReceiverPort = port;
        _activeDownloadFolder = downloadFolder;

        AppendLog($"Receiver started on port {port}, folder {downloadFolder}");
        RefreshStatus();
    }

    private void StopReceiver()
    {
        if (_receiverProcess is not { HasExited: false })
        {
            return;
        }

        try
        {
            _receiverProcess.Kill(entireProcessTree: true);
            _receiverProcess.WaitForExit(2000);
            AppendLog("Receiver stopped.");
        }
        catch (Exception ex)
        {
            AppendLog($"Failed to stop receiver: {ex.Message}");
        }
        finally
        {
            _receiverProcess = null;
            _activeReceiverPort = null;
            _activeDownloadFolder = null;
            RefreshStatus();
        }
    }

    private async Task DiscoverDevicesAsync()
    {
        if (!ushort.TryParse(_portTextBox.Text.Trim(), out var port))
        {
            ShowError("Listen port is invalid.");
            return;
        }

        _discoverButton.Enabled = false;
        try
        {
            var output = await Task.Run(() =>
            {
                var psi = new ProcessStartInfo
                {
                    FileName = _exePath,
                    WorkingDirectory = _projectRoot,
                    Arguments = $"discover {port} 5",
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                };
                using var process = Process.Start(psi)!;
                var stdout = process.StandardOutput.ReadToEnd();
                var stderr = process.StandardError.ReadToEnd();
                process.WaitForExit();
                return string.IsNullOrWhiteSpace(stderr) ? stdout : $"{stdout}\n{stderr}";
            });

            AppendLog("Discovery finished.");
            ShowInfo(output.Trim());
            RefreshStatus();
        }
        catch (Exception ex)
        {
            ShowError($"Discovery failed: {ex.Message}");
        }
        finally
        {
            _discoverButton.Enabled = true;
        }
    }

    private void OpenDownloads()
    {
        var downloadFolder = _receiverProcess is { HasExited: false } && !string.IsNullOrWhiteSpace(_activeDownloadFolder)
            ? _activeDownloadFolder!
            : ResolveDownloadFolder();
        Directory.CreateDirectory(downloadFolder);
        Process.Start("explorer.exe", downloadFolder);
    }

    private void BrowseSendFile()
    {
        using var dialog = new OpenFileDialog
        {
            CheckFileExists = true,
            CheckPathExists = true,
            Multiselect = false,
            Title = "Choose File To Send",
        };
        if (dialog.ShowDialog(this) == DialogResult.OK)
        {
            _sendFileTextBox.Text = dialog.FileName;
            _sendPayloadKind = SendPayloadKind.File;
            _sendSelectionLabel.Text = $"Current selection: file  {dialog.FileName}";
        }
    }

    private void BrowseSendFolder()
    {
        using var dialog = new FolderBrowserDialog();
        var currentPath = _sendFileTextBox.Text.Trim();
        if (Directory.Exists(currentPath))
        {
            dialog.SelectedPath = currentPath;
        }

        if (dialog.ShowDialog(this) == DialogResult.OK)
        {
            _sendFileTextBox.Text = dialog.SelectedPath;
            _sendPayloadKind = SendPayloadKind.Folder;
            _sendSelectionLabel.Text = $"Current selection: folder  {dialog.SelectedPath}";
        }
    }

    private async Task SendFileAsync()
    {
        if (_sendTargetComboBox.SelectedItem is not SendTargetItem target)
        {
            ShowError("Choose a paired device with a known host and port first.");
            return;
        }

        var sourcePath = _sendFileTextBox.Text.Trim();
        if (string.IsNullOrWhiteSpace(sourcePath))
        {
            ShowError("Choose a valid file or folder to send.");
            return;
        }

        _sendFileButton.Enabled = false;
        _browseSendFileButton.Enabled = false;
        _browseSendFolderButton.Enabled = false;
        try
        {
            var payload = await Task.Run(() => PrepareSendPayload(sourcePath));
            AppendLog($"Sending {payload.DisplayName} to {target.DeviceId} at {target.Host}:{target.Port} ...");
            var output = await Task.Run(() =>
            {
                var psi = new ProcessStartInfo
                {
                    FileName = _exePath,
                    WorkingDirectory = _projectRoot,
                    Arguments = $"send {target.Host} {target.Port} {target.DeviceId} \"{payload.FilePath}\"",
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                };
                using var process = Process.Start(psi)!;
                var stdout = process.StandardOutput.ReadToEnd();
                var stderr = process.StandardError.ReadToEnd();
                process.WaitForExit();
                if (process.ExitCode != 0)
                {
                    return string.IsNullOrWhiteSpace(stderr) ? stdout : $"{stdout}\n{stderr}";
                }
                return string.IsNullOrWhiteSpace(stdout) ? "File sent successfully." : stdout;
            });

            AppendLog(output.Trim());
            ShowInfo(output.Trim());
            if (payload.DeleteAfterSend)
            {
                TryDeleteTemporaryPayload(payload.FilePath);
            }
        }
        catch (Exception ex)
        {
            ShowError($"Send failed: {ex.Message}");
        }
        finally
        {
            _sendFileButton.Enabled = _sendTargetComboBox.Items.Count > 0;
            _browseSendFileButton.Enabled = true;
            _browseSendFolderButton.Enabled = true;
        }
    }

    private PreparedSendPayload PrepareSendPayload(string sourcePath)
    {
        if (_sendPayloadKind == SendPayloadKind.Folder || Directory.Exists(sourcePath))
        {
            if (!Directory.Exists(sourcePath))
            {
                throw new InvalidOperationException("Selected folder does not exist.");
            }

            var directoryName = Path.GetFileName(sourcePath.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
            if (string.IsNullOrWhiteSpace(directoryName))
            {
                directoryName = "folder";
            }

            var tempZipPath = Path.Combine(
                Path.GetTempPath(),
                $"lan-transfer-{SanitizeFileComponent(directoryName)}-{DateTime.Now:yyyyMMdd-HHmmss}.zip");

            if (File.Exists(tempZipPath))
            {
                File.Delete(tempZipPath);
            }

            ZipFile.CreateFromDirectory(sourcePath, tempZipPath, CompressionLevel.Fastest, includeBaseDirectory: true);
            return new PreparedSendPayload(tempZipPath, $"{directoryName}.zip", true);
        }

        if (!File.Exists(sourcePath))
        {
            throw new InvalidOperationException("Selected file does not exist.");
        }

        return new PreparedSendPayload(sourcePath, Path.GetFileName(sourcePath), false);
    }

    private void TryDeleteTemporaryPayload(string filePath)
    {
        try
        {
            if (File.Exists(filePath))
            {
                File.Delete(filePath);
            }
        }
        catch (Exception ex)
        {
            AppendLog($"Temporary zip cleanup failed: {ex.Message}");
        }
    }

    private CodeLookupMatch? ResolveConnectionCode(string code)
    {
        using var client = new UdpClient();
        client.EnableBroadcast = true;
        client.Client.ReceiveTimeout = 800;
        client.Client.SendTimeout = 1200;
        client.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
        client.Client.Bind(new IPEndPoint(IPAddress.Any, 0));

        var payload = Encoding.UTF8.GetBytes($"LFT_CODE_LOOKUP|1|{code}");
        var targets = GetCodeLookupTargets();
        foreach (var target in targets)
        {
            try
            {
                client.Send(payload, payload.Length, new IPEndPoint(target, 38561));
                AppendLog($"Code lookup broadcast sent to {target}");
            }
            catch (SocketException ex)
            {
                AppendLog($"Code lookup send failed on {target}: {ex.Message}");
            }
        }

        var deadline = DateTime.UtcNow.AddSeconds(4);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                IPEndPoint remoteEndPoint = new(IPAddress.Any, 0);
                var responseBytes = client.Receive(ref remoteEndPoint);
                var response = Encoding.UTF8.GetString(responseBytes);
                var match = ParseCodeLookupResponse(code, response, remoteEndPoint.Address.ToString());
                if (match is not null)
                {
                    return match;
                }
            }
            catch (SocketException)
            {
                break;
            }
        }

        return null;
    }

    private static List<IPAddress> GetCodeLookupTargets()
    {
        var targets = new HashSet<IPAddress>();
        targets.Add(IPAddress.Broadcast);

        foreach (var networkInterface in NetworkInterface.GetAllNetworkInterfaces())
        {
            if (networkInterface.OperationalStatus != OperationalStatus.Up)
            {
                continue;
            }
            if (networkInterface.NetworkInterfaceType is NetworkInterfaceType.Loopback or NetworkInterfaceType.Tunnel)
            {
                continue;
            }

            var properties = networkInterface.GetIPProperties();
            foreach (var unicast in properties.UnicastAddresses)
            {
                if (unicast.Address.AddressFamily != AddressFamily.InterNetwork)
                {
                    continue;
                }
                if (IPAddress.IsLoopback(unicast.Address))
                {
                    continue;
                }
                if (unicast.IPv4Mask is null)
                {
                    continue;
                }

                var broadcast = CalculateBroadcastAddress(unicast.Address, unicast.IPv4Mask);
                targets.Add(broadcast);
            }
        }

        return targets.ToList();
    }

    private static IPAddress CalculateBroadcastAddress(IPAddress address, IPAddress subnetMask)
    {
        var addressBytes = address.GetAddressBytes();
        var maskBytes = subnetMask.GetAddressBytes();
        if (addressBytes.Length != 4 || maskBytes.Length != 4)
        {
            return IPAddress.Broadcast;
        }

        var broadcastBytes = new byte[4];
        for (var index = 0; index < 4; index++)
        {
            broadcastBytes[index] = (byte)(addressBytes[index] | ~maskBytes[index]);
        }
        return new IPAddress(broadcastBytes);
    }

    private static CodeLookupMatch? ParseCodeLookupResponse(string expectedCode, string payload, string host)
    {
        var fields = SplitEscapedFields(payload);
        if (fields.Count != 6)
        {
            return null;
        }

        if (!string.Equals(fields[0], "LFT_CODE_MATCH", StringComparison.Ordinal) ||
            !string.Equals(fields[1], "1", StringComparison.Ordinal) ||
            !string.Equals(fields[2], expectedCode, StringComparison.Ordinal))
        {
            return null;
        }

        if (!int.TryParse(fields[5], out var port) || port <= 0 || port > 65535)
        {
            return null;
        }

        return new CodeLookupMatch(fields[3], fields[4], host, port);
    }

    private static List<string> SplitEscapedFields(string payload)
    {
        var fields = new List<string>();
        var current = new StringBuilder(payload.Length);
        var escaping = false;

        foreach (var ch in payload)
        {
            if (escaping)
            {
                current.Append(ch);
                escaping = false;
                continue;
            }

            if (ch == '\\')
            {
                escaping = true;
                continue;
            }

            if (ch == '|')
            {
                fields.Add(current.ToString());
                current.Clear();
                continue;
            }

            current.Append(ch);
        }

        if (escaping)
        {
            return fields;
        }

        fields.Add(current.ToString());
        return fields;
    }

    private void RemoveSelectedPairedDevice()
    {
        if (string.IsNullOrWhiteSpace(_selectedPairedDeviceId))
        {
            return;
        }

        var pairedDevicesPath = Path.Combine(_projectRoot, "paired_devices.json");
        if (!File.Exists(pairedDevicesPath))
        {
            return;
        }

        try
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(pairedDevicesPath));
            if (!doc.RootElement.TryGetProperty("devices", out var devices) || devices.ValueKind != JsonValueKind.Array)
            {
                return;
            }

            var remaining = new List<(string DeviceId, string Host, int Port)>();
            foreach (var device in devices.EnumerateArray())
            {
                var id = device.TryGetProperty("device_id", out var idValue) ? idValue.GetString() ?? "" : "";
                if (id == _selectedPairedDeviceId)
                {
                    continue;
                }

                var host = device.TryGetProperty("host", out var hostValue) ? hostValue.GetString() ?? "" : "";
                var port = device.TryGetProperty("port", out var portValue) ? portValue.GetInt32() : 0;
                remaining.Add((id, host, port));
            }

            var output = new StringBuilder();
            output.AppendLine("{");
            output.AppendLine("  \"devices\": [");
            for (var index = 0; index < remaining.Count; index++)
            {
                var entry = remaining[index];
                output.AppendLine("    {");
                output.AppendLine($"      \"device_id\": \"{EscapeJson(entry.DeviceId)}\",");
                output.AppendLine($"      \"host\": \"{EscapeJson(entry.Host)}\",");
                output.AppendLine($"      \"port\": {entry.Port}");
                output.Append("    }");
                if (index + 1 < remaining.Count)
                {
                    output.Append(",");
                }
                output.AppendLine();
            }
            output.AppendLine("  ]");
            output.AppendLine("}");

            File.WriteAllText(pairedDevicesPath, output.ToString(), Encoding.UTF8);
            AppendLog($"Removed paired device: {_selectedPairedDeviceId}");
            _selectedPairedDeviceId = null;
            RefreshStatus();
        }
        catch (Exception ex)
        {
            ShowError($"Failed to remove paired device: {ex.Message}");
        }
    }

    private void ReceiverOutputDataReceived(object sender, DataReceivedEventArgs e)
    {
        if (e.Data is null)
        {
            return;
        }

        BeginInvoke(() =>
        {
            AppendLog(e.Data);
            HandlePromptLine(e.Data);
            RefreshStatus();
        });
    }

    private void ReceiverErrorDataReceived(object sender, DataReceivedEventArgs e)
    {
        if (e.Data is null)
        {
            return;
        }

        BeginInvoke(() => AppendLog($"ERR: {e.Data}"));
    }

    private void HandlePromptLine(string line)
    {
        if (_receiverProcess is not { HasExited: false })
        {
            return;
        }

        if (line.StartsWith("Allow pairing with device ", StringComparison.Ordinal))
        {
            var result = MessageBox.Show(
                this,
                line,
                "Pair Request",
                MessageBoxButtons.YesNo,
                MessageBoxIcon.Question);
            SendToReceiver(result == DialogResult.Yes ? "y" : "n");
            return;
        }

        if (line.StartsWith("Accept file transfer? [y/N]:", StringComparison.Ordinal))
        {
            var result = MessageBox.Show(
                this,
                "Accept incoming file transfer?",
                "File Request",
                MessageBoxButtons.YesNo,
                MessageBoxIcon.Question);
            SendToReceiver(result == DialogResult.Yes ? "y" : "n");
        }
    }

    private void SendToReceiver(string text)
    {
        if (_receiverProcess is not { HasExited: false })
        {
            return;
        }

        lock (_stdinLock)
        {
            _receiverProcess.StandardInput.WriteLine(text);
            _receiverProcess.StandardInput.Flush();
        }
    }

    private void AppendLog(string message)
    {
        var timestamp = DateTime.Now.ToString("HH:mm:ss");
        var entry = $"[{timestamp}] {message}";
        _logTextBox.Text = string.IsNullOrWhiteSpace(_logTextBox.Text)
            ? entry
            : $"{entry}{Environment.NewLine}{_logTextBox.Text}";
    }

    private void ShowError(string message)
    {
        MessageBox.Show(this, message, "LAN Transfer", MessageBoxButtons.OK, MessageBoxIcon.Error);
    }

    private void ShowInfo(string message)
    {
        MessageBox.Show(this, message, "LAN Transfer", MessageBoxButtons.OK, MessageBoxIcon.Information);
    }

    private static string MakeConnectionCode(string deviceId)
    {
        uint hash = 2166136261;
        foreach (var ch in Encoding.UTF8.GetBytes(deviceId))
        {
            hash ^= ch;
            hash *= 16777619;
        }

        var codeValue = 100000u + (hash % 900000u);
        return codeValue.ToString("D6");
    }

    private static string EscapeJson(string value)
    {
        return value
            .Replace("\\", "\\\\", StringComparison.Ordinal)
            .Replace("\"", "\\\"", StringComparison.Ordinal);
    }

    private static (string ProjectRoot, string ExePath) ResolveAppPaths()
    {
        var appDirectory = AppContext.BaseDirectory;
        var packagedExePath = Path.Combine(appDirectory, "lan_transfer.exe");
        if (File.Exists(packagedExePath))
        {
            return (appDirectory, packagedExePath);
        }

        var repoRoot = Path.GetFullPath(Path.Combine(appDirectory, "..", "..", "..", ".."));
        var devExePath = Path.Combine(repoRoot, "build", "Release", "lan_transfer.exe");
        return (repoRoot, devExePath);
    }

    private static string SanitizeFileComponent(string value)
    {
        var invalidChars = Path.GetInvalidFileNameChars();
        var builder = new StringBuilder(value.Length);
        foreach (var ch in value)
        {
            builder.Append(invalidChars.Contains(ch) ? '_' : ch);
        }
        return builder.ToString();
    }

    private sealed record PairedDeviceRecord(string DeviceId, string Host, int Port);

    private sealed record PreparedSendPayload(string FilePath, string DisplayName, bool DeleteAfterSend);

    private sealed record CodeLookupMatch(string DeviceId, string DeviceName, string Host, int Port);

    private sealed record SendTargetItem(string DeviceId, string Host, int Port)
    {
        public override string ToString()
        {
            return $"{DeviceId}  {Host}:{Port}";
        }
    }

    private enum SendPayloadKind
    {
        None,
        File,
        Folder,
    }
}
