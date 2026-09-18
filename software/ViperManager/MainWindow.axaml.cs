using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Threading;
using ViperManager.Models;
using ViperManager.Services;

namespace ViperManager;

public partial class MainWindow : Window
{
    private readonly DeviceService _device = new();
    private readonly TotpVaultService _vaultService = new();
    private readonly DispatcherTimer _totpTimer = new();
    private readonly DispatcherTimer _notificationTimer = new();
    private readonly DispatcherTimer _guardHeartbeatTimer = new();
    private bool _isWindowsLocked = false;

    private ObservableCollection<TotpAccount> _totpAccounts = new();
    private Action? _pendingDialogConfirmAction;

    // Vector Icon Geometries
    private static readonly Geometry EyeOpenGeom = StreamGeometry.Parse("M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5c-1.73-4.39-6-7.5-11-7.5zM12 17c-2.76 0-5-2.24-5-5s2.24-5 5-5 5 2.24 5 5-2.24 5-5 5zm0-8c-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3-1.34-3-3-3z");
    private static readonly Geometry EyeSlashGeom = StreamGeometry.Parse("M12 7c2.76 0 5 2.24 5 5 0 .65-.13 1.26-.36 1.83l2.92 2.92c1.51-1.26 2.7-2.89 3.44-4.75-1.73-4.39-6-7.5-11-7.5-1.4 0-2.74.25-3.98.7l2.16 2.16C10.74 7.13 11.35 7 12 7zM2 4.27l2.28 2.28.46.46C3.08 8.3 1.78 10.02 1 12c1.73 4.39 6 7.5 11 7.5 1.55 0 3.03-.3 4.38-.84l.42.42L19.73 22 21 20.73 3.27 3 2 4.27zM7.53 9.8l1.55 1.55c-.05.21-.08.43-.08.65 0 1.66 1.34 3 3 3 .22 0 .44-.03.65-.08l1.55 1.55c-.67.33-1.41.53-2.2.53-2.76 0-5-2.24-5-5 0-.79.2-1.53.53-2.2zm4.31-.78l3.15 3.15.02-.16c0-1.66-1.34-3-3-3l-.17.01z");
    private static readonly Geometry FingerprintGeom = StreamGeometry.Parse("M17.81 4.47c-.08 0-.16-.02-.23-.06C15.66 3.42 14 3 12.01 3c-1.98 0-3.86.47-5.57 1.41-.24.13-.54.04-.68-.2-.13-.24-.04-.55.2-.68C7.82 2.52 9.86 2 12.01 2c2.13 0 3.99.47 6.03 1.52.25.13.34.43.21.67-.09.18-.26.28-.44.28zm2.68 2.96c-.22.15-.53.08-.68-.15-.79-1.22-1.9-2.2-3.2-2.85-.25-.12-.35-.42-.23-.67.12-.25.42-.35.67-.23 1.48.74 2.74 1.86 3.63 3.23.14.23.07.53-.19.67zM12 9c-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3-1.34-3-3-3z");
    private static readonly Geometry LiftArrowGeom = StreamGeometry.Parse("M12 4l-8 8h5v8h6v-8h5z");
    private static readonly Geometry CheckmarkGeom = StreamGeometry.Parse("M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z");
    private static readonly Geometry AlertGeom = StreamGeometry.Parse("M1 21h22L12 2 1 21zm12-3h-2v-2h2v2zm0-4h-2v-4h2v4z");

    public MainWindow()
    {
        InitializeComponent();

        // Detect and display Host OS
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            TxtHostOs.Text = "Windows 11";
        else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
            TxtHostOs.Text = "macOS";
        else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
            TxtHostOs.Text = "Linux";

        // Load saved TOTP accounts
        var loaded = _vaultService.LoadAccounts();
        _totpAccounts = new ObservableCollection<TotpAccount>(loaded);
        ListTotpAccounts.ItemsSource = _totpAccounts;

        // Hook device events
        _device.LineReceived += OnDeviceLineReceived;
        _device.ConnectionStateChanged += OnConnectionStateChanged;
        _device.EnrollmentStepReceived += OnEnrollmentStepReceived;
        _device.SshPubReceived += OnSshPubReceived;
        _device.SshSigReceived += OnSshSigReceived;

        // Setup TOTP 1-second interval timer
        _totpTimer.Interval = TimeSpan.FromSeconds(1);
        _totpTimer.Tick += (s, e) => UpdateAllTotpCodes();
        _totpTimer.Start();
        UpdateAllTotpCodes();

        // Setup Notification auto-dismiss timer
        _notificationTimer.Interval = TimeSpan.FromSeconds(3.5);
        _notificationTimer.Tick += (s, e) =>
        {
            _notificationTimer.Stop();
            BannerNotification.IsVisible = false;
        };

        // Setup Lock Guard Heartbeat Timer (syncs state every 2.5s)
        _guardHeartbeatTimer.Interval = TimeSpan.FromSeconds(2.5);
        _guardHeartbeatTimer.Tick += (s, e) =>
        {
            if (_device.IsConnected)
            {
                _device.SendCommand(_isWindowsLocked ? "CMD:LOCK" : "CMD:UNLOCK");
            }
        };

        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
        {
            Microsoft.Win32.SystemEvents.SessionSwitch += OnSessionSwitch;
        }

        // Refresh ports on launch
        RefreshPorts();

        Closed += (s, e) =>
        {
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            {
                Microsoft.Win32.SystemEvents.SessionSwitch -= OnSessionSwitch;
            }
            _guardHeartbeatTimer.Stop();
            _device.Dispose();
        };
    }

    private void OnSessionSwitch(object sender, Microsoft.Win32.SessionSwitchEventArgs e)
    {
        if (e.Reason == Microsoft.Win32.SessionSwitchReason.SessionLock)
        {
            _isWindowsLocked = true;
            _device.SendCommand("CMD:LOCK");
        }
        else if (e.Reason == Microsoft.Win32.SessionSwitchReason.SessionUnlock)
        {
            _isWindowsLocked = false;
            _device.SendCommand("CMD:UNLOCK");
        }
    }

    #region Window Dragging

    private void OnTitleBarPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
        {
            if (e.ClickCount == 2)
            {
                WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
            }
            else
            {
                BeginMoveDrag(e);
            }
        }
    }

    #endregion

    #region In-App Notifications & Confirmations

    public void ShowNotification(string message, bool isError = false)
    {
        Dispatcher.UIThread.Post(() =>
        {
            TxtNotification.Text = message;
            if (isError)
            {
                BannerNotification.Background = new SolidColorBrush(Color.Parse("#33181C"));
                BannerNotification.BorderBrush = new SolidColorBrush(Color.Parse("#EF4444"));
                IconNotification.Data = AlertGeom;
                IconNotification.Foreground = new SolidColorBrush(Color.Parse("#EF4444"));
            }
            else
            {
                BannerNotification.Background = new SolidColorBrush(Color.Parse("#162A45"));
                BannerNotification.BorderBrush = new SolidColorBrush(Color.Parse("#0067C0"));
                IconNotification.Data = CheckmarkGeom;
                IconNotification.Foreground = new SolidColorBrush(Color.Parse("#60CDFF"));
            }

            BannerNotification.IsVisible = true;
            _notificationTimer.Stop();
            _notificationTimer.Start();
        });
    }

    private void BtnCloseNotification_Click(object? sender, RoutedEventArgs e)
    {
        _notificationTimer.Stop();
        BannerNotification.IsVisible = false;
    }

    public void ShowConfirmation(string title, string message, Action onConfirm, bool isDanger = true)
    {
        TxtDialogTitle.Text = title;
        TxtDialogMessage.Text = message;
        BtnDialogConfirm.Classes.Clear();
        BtnDialogConfirm.Classes.Add(isDanger ? "Danger" : "Accent");
        BtnDialogConfirm.Content = isDanger ? "Delete / Wipe" : "Confirm";

        _pendingDialogConfirmAction = onConfirm;
        DialogOverlay.IsVisible = true;
    }

    private void BtnDialogCancel_Click(object? sender, RoutedEventArgs e)
    {
        DialogOverlay.IsVisible = false;
        _pendingDialogConfirmAction = null;
    }

    private void BtnDialogConfirm_Click(object? sender, RoutedEventArgs e)
    {
        DialogOverlay.IsVisible = false;
        var act = _pendingDialogConfirmAction;
        _pendingDialogConfirmAction = null;
        act?.Invoke();
    }

    #endregion

    #region Port & Connection Management

    private async Task RefreshPortsAsync()
    {
        BtnRefreshPorts.IsEnabled = false;
        TxtStatusDetail.Text = "Scanning for USB & Bluetooth hardware...";
        var devices = await _device.GetCompatibleDevicesAsync();
        BtnRefreshPorts.IsEnabled = true;

        if (devices.Count > 0)
        {
            CboPorts.ItemsSource = devices;
            CboPorts.SelectedIndex = 0;
            BtnConnect.IsEnabled = true;
            TxtStatusDetail.Text = $"{devices.Count} compatible Viper hardware key(s) detected.";
        }
        else
        {
            var empty = new System.Collections.Generic.List<SerialDeviceInfo>
            {
                new SerialDeviceInfo { PortName = "", DisplayName = "No Viper Key Detected", IsCompatible = false }
            };
            CboPorts.ItemsSource = empty;
            CboPorts.SelectedIndex = 0;
            BtnConnect.IsEnabled = false;
            TxtStatusDetail.Text = "Please turn on your Viper Key via Bluetooth or connect USB cable.";
        }
    }

    private void RefreshPorts()
    {
        _ = RefreshPortsAsync();
    }

    private async void BtnRefreshPorts_Click(object? sender, RoutedEventArgs e)
    {
        await RefreshPortsAsync();
        AppendConsole("[App] Scanned for USB & Bluetooth hardware.");
        ShowNotification("Device scan complete.");
    }

    private async void BtnConnect_Click(object? sender, RoutedEventArgs e)
    {
        if (_device.IsConnected)
        {
            _device.Disconnect();
            return;
        }

        if (CboPorts.SelectedItem is not SerialDeviceInfo dev || !dev.IsCompatible)
        {
            ShowNotification("No compatible Viper Key selected. Please turn on your device or plug it in.", true);
            return;
        }

        BtnConnect.IsEnabled = false;
        BtnConnect.Content = "Connecting...";

        bool ok = await _device.ConnectAsync(dev);
        BtnConnect.IsEnabled = true;

        if (!ok)
        {
            ShowNotification($"Failed to connect to {dev.DisplayName}.", true);
        }
    }

    private void OnConnectionStateChanged(bool isConnected, string info)
    {
        Dispatcher.UIThread.Post(() =>
        {
            if (isConnected)
            {
                BtnConnect.Content = "Disconnect";
                BtnConnect.Classes.Clear();
                BtnConnect.Classes.Add("Danger");

                DotStatus.Fill = new SolidColorBrush(Color.Parse("#10B981"));
                TxtConnStatus.Text = "Connected";
                TxtConnStatus.Foreground = new SolidColorBrush(Color.Parse("#10B981"));

                _isWindowsLocked = false;
                _device.SendCommand("CMD:UNLOCK");
                _guardHeartbeatTimer.Start();

                if (_device.IsServiceLinked)
                {
                    TxtServiceStatus.Text = "Paused (Manager Open)";
                    TxtServiceStatus.Foreground = new SolidColorBrush(Color.Parse("#F59E0B"));
                }
                else
                {
                    TxtServiceStatus.Text = "Direct USB Mode";
                    TxtServiceStatus.Foreground = new SolidColorBrush(Color.Parse("#60CDFF"));
                }
                AppendConsole("[App] Connected to Viper Biometric Key (USB).");
                ShowNotification("Connected via USB.");
            }
            else
            {
                _guardHeartbeatTimer.Stop();
                BtnConnect.Content = "Connect Device";
                BtnConnect.Classes.Clear();
                BtnConnect.Classes.Add("Accent");

                DotStatus.Fill = new SolidColorBrush(Color.Parse("#EF4444"));
                TxtConnStatus.Text = "Disconnected";
                TxtConnStatus.Foreground = new SolidColorBrush(Color.Parse("#94A3B8"));
                TxtStatusDetail.Text = "Select hardware device above and connect.";

                TxtServiceStatus.Text = "Standby";
                TxtServiceStatus.Foreground = new SolidColorBrush(Color.Parse("#94A3B8"));

                AppendConsole($"[App] {info}");
                ShowNotification("Device disconnected.");
            }
        });
    }

    #endregion

    #region Navigation

    private void NavButton_Click(object? sender, RoutedEventArgs e)
    {
        if (sender is not Button btn || btn.Tag is not string tag) return;

        var defaultBg = Brushes.Transparent;
        var defaultFg = new SolidColorBrush(Color.Parse("#94A3B8"));
        var activeBg = new SolidColorBrush(Color.Parse("#1A2230"));
        var activeFg = new SolidColorBrush(Color.Parse("#F8FAFC"));

        NavPasswords.Background = defaultBg; NavPasswords.Foreground = defaultFg;
        NavSsh.Background = defaultBg; NavSsh.Foreground = defaultFg;
        NavTotp.Background = defaultBg; NavTotp.Foreground = defaultFg;
        NavFingerprints.Background = defaultBg; NavFingerprints.Foreground = defaultFg;
        NavConsole.Background = defaultBg; NavConsole.Foreground = defaultFg;

        btn.Background = activeBg;
        btn.Foreground = activeFg;

        PanelPasswords.IsVisible = false;
        PanelSsh.IsVisible = false;
        PanelTotp.IsVisible = false;
        PanelFingerprints.IsVisible = false;
        PanelConsole.IsVisible = false;

        switch (tag)
        {
            case "Passwords": PanelPasswords.IsVisible = true; break;
            case "Ssh": PanelSsh.IsVisible = true; break;
            case "Totp": PanelTotp.IsVisible = true; break;
            case "Fingerprints": PanelFingerprints.IsVisible = true; break;
            case "Console": PanelConsole.IsVisible = true; break;
        }
    }

    #endregion

    #region OS Passwords

    private void BtnReveal_Click(object? sender, RoutedEventArgs e)
    {
        if (sender is Button btn && btn.Tag is string targetName)
        {
            var target = this.FindControl<TextBox>(targetName);
            var icon = btn.Content as PathIcon;
            if (target != null && icon != null)
            {
                if (target.PasswordChar == '•')
                {
                    target.PasswordChar = '\0';
                    icon.Data = EyeSlashGeom;
                }
                else
                {
                    target.PasswordChar = '•';
                    icon.Data = EyeOpenGeom;
                }
            }
        }
    }

    private void BtnSaveWin_Click(object? sender, RoutedEventArgs e)
    {
        if (!_device.IsConnected) { ShowNotification("Connect device first.", true); return; }
        _device.SendCommand($"SET:WIN:{TxtPassWin.Text}");
        AppendConsole("[App] Sent Windows password to hardware.");
        ShowNotification("Windows password saved to hardware vault!");
    }

    private void BtnSaveMac_Click(object? sender, RoutedEventArgs e)
    {
        if (!_device.IsConnected) { ShowNotification("Connect device first.", true); return; }
        _device.SendCommand($"SET:MAC:{TxtPassMac.Text}");
        AppendConsole("[App] Sent macOS password to hardware.");
        ShowNotification("macOS password saved to hardware vault!");
    }

    private void BtnSaveLin_Click(object? sender, RoutedEventArgs e)
    {
        if (!_device.IsConnected) { ShowNotification("Connect device first.", true); return; }
        _device.SendCommand($"SET:LIN:{TxtPassLin.Text}");
        AppendConsole("[App] Sent Linux password to hardware.");
        ShowNotification("Linux password saved to hardware vault!");
    }

    private void BtnSaveAllPass_Click(object? sender, RoutedEventArgs e)
    {
        if (!_device.IsConnected) { ShowNotification("Connect device first.", true); return; }
        if (!string.IsNullOrEmpty(TxtPassWin.Text)) _device.SendCommand($"SET:WIN:{TxtPassWin.Text}");
        if (!string.IsNullOrEmpty(TxtPassMac.Text)) _device.SendCommand($"SET:MAC:{TxtPassMac.Text}");
        if (!string.IsNullOrEmpty(TxtPassLin.Text)) _device.SendCommand($"SET:LIN:{TxtPassLin.Text}");
        AppendConsole("[App] Saved all configured credentials to hardware.");
        ShowNotification("All configured OS credentials saved to hardware!");
    }

    private void BtnSyncHost_Click(object? sender, RoutedEventArgs e)
    {
        if (!_device.IsConnected) { ShowNotification("Connect device first.", true); return; }
        _device.SyncHostOs();
        _device.SyncTime();
        AppendConsole("[App] Host OS and Time synchronized with hardware.");
        ShowNotification("Hardware Host OS profile and clock synchronized!");
    }

    #endregion

    #region SSH Hardware Keys

    private void BtnGenSsh_Click(object? sender, RoutedEventArgs e)
    {
        if (!_device.IsConnected) { ShowNotification("Connect device first.", true); return; }
        AppendConsole("[SSH] Requesting on-device ECDSA P-256 keypair generation...");
        _device.SendCommand("CMD:SSH_GEN");
        ShowNotification("Generating unextractable ECDSA keypair on hardware...");
    }

    private void BtnReadSshPub_Click(object? sender, RoutedEventArgs e)
    {
        if (!_device.IsConnected) { ShowNotification("Connect device first.", true); return; }
        AppendConsole("[SSH] Reading public key from hardware...");
        _device.SendCommand("CMD:SSH_PUB");
        ShowNotification("Reading public key from device...");
    }

    private void OnSshPubReceived(string hexPub)
    {
        Dispatcher.UIThread.Post(() =>
        {
            TxtRawSshHex.Text = hexPub;
            if (string.IsNullOrWhiteSpace(hexPub) || hexPub == "00" || hexPub.Length != 130)
            {
                TxtOpenSshKey.Text = "No valid key found on hardware. Click 'Generate Keypair on Device'.";
                ShowNotification("No key found on hardware. Click 'Generate Keypair on Device'.", true);
            }
            else
            {
                string openSsh = SshKeyFormatter.FormatOpenSsh(hexPub);
                TxtOpenSshKey.Text = openSsh;
                AppendConsole($"[SSH] Public key retrieved ({hexPub.Length / 2} bytes).");
                ShowNotification("SSH Public Key loaded successfully!");
            }
        });
    }

    private async void BtnCopySsh_Click(object? sender, RoutedEventArgs e)
    {
        if (string.IsNullOrWhiteSpace(TxtOpenSshKey.Text) || TxtOpenSshKey.Text.StartsWith("No valid"))
        {
            ShowNotification("Generate or read a key first.", true);
            return;
        }
        if (Clipboard != null)
        {
            await Clipboard.SetTextAsync(TxtOpenSshKey.Text);
            AppendConsole("[App] OpenSSH public key copied to clipboard.");
            ShowNotification("OpenSSH Public Key copied to clipboard! Ready for GitHub/GitLab.");
        }
    }

    private void BtnTestSshSign_Click(object? sender, RoutedEventArgs e)
    {
        if (!_device.IsConnected) { ShowNotification("Connect device first.", true); return; }
        TxtSshSignResult.Text = "Waiting for physical touch on sensor (Purple LED)...";
        TxtSshSignResult.Foreground = new SolidColorBrush(Color.Parse("#A78BFA"));

        const string testHash = "a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2";
        _device.SendCommand($"CMD:SSH_SIGN:{testHash}");
        ShowNotification("Challenge sent! Touch your Viper Key to sign.");
    }

    private void OnSshSigReceived(string sigHex)
    {
        Dispatcher.UIThread.Post(() =>
        {
            if (sigHex == "FAILED")
            {
                TxtSshSignResult.Text = "Signature challenge failed or timed out.";
                TxtSshSignResult.Foreground = new SolidColorBrush(Color.Parse("#EF4444"));
                AppendConsole("[SSH] Hardware signature challenge failed or timed out.");
                ShowNotification("Signature challenge failed or timed out.", true);
            }
            else
            {
                TxtSshSignResult.Text = $"Signature Verified: {sigHex.Substring(0, Math.Min(18, sigHex.Length))}...";
                TxtSshSignResult.Foreground = new SolidColorBrush(Color.Parse("#10B981"));
                AppendConsole($"[SSH] Signature SUCCESS! Raw signature: {sigHex}");
                ShowNotification("Hardware signature verified successfully!");
            }
        });
    }

    #endregion

    #region Multi-Account TOTP 2FA Vault

    private void UpdateAllTotpCodes()
    {
        foreach (var acc in _totpAccounts)
        {
            var (code, sec) = TotpHelper.GenerateLiveCode(acc.Secret);
            acc.LiveCode = code.Length == 6 ? $"{code.Substring(0, 3)} {code.Substring(3, 3)}" : code;
            acc.SecondsLeft = sec;
        }
    }

    private void BtnAddTotpAccount_Click(object? sender, RoutedEventArgs e)
    {
        string name = TxtNewTotpName.Text?.Trim() ?? string.Empty;
        string secret = TxtNewTotpSecret.Text?.Trim().ToUpperInvariant() ?? string.Empty;

        if (string.IsNullOrWhiteSpace(name))
        {
            ShowNotification("Please enter an account name (e.g. GitHub).", true);
            return;
        }
        if (string.IsNullOrWhiteSpace(secret))
        {
            ShowNotification("Please enter a Base32 secret seed.", true);
            return;
        }

        var newAcc = new TotpAccount { Name = name, Secret = secret };
        _totpAccounts.Add(newAcc);
        _vaultService.SaveAccounts(_totpAccounts.ToList());

        TxtNewTotpName.Text = string.Empty;
        TxtNewTotpSecret.Text = string.Empty;

        UpdateAllTotpCodes();
        ShowNotification($"Added '{name}' to TOTP 2FA Vault!");
    }

    private async void BtnCopyAccountCode_Click(object? sender, RoutedEventArgs e)
    {
        if (sender is Button btn && btn.Tag is TotpAccount acc)
        {
            string cleanCode = acc.LiveCode.Replace(" ", "");
            if (cleanCode.Length == 6 && Clipboard != null)
            {
                await Clipboard.SetTextAsync(cleanCode);
                AppendConsole($"[App] Live code for '{acc.Name}' copied: {cleanCode}");
                ShowNotification($"Copied 2FA code for '{acc.Name}': {cleanCode}");
            }
        }
    }

    private void BtnSyncAccountToKey_Click(object? sender, RoutedEventArgs e)
    {
        if (sender is Button btn && btn.Tag is TotpAccount acc)
        {
            if (!_device.IsConnected) { ShowNotification("Connect device first.", true); return; }
            _device.SendCommand($"SET:TOTP:{acc.Secret}");
            TxtActiveHardwareTotp.Text = $"Active Key Seed: {acc.Name} ({acc.MaskedSecret})";
            AppendConsole($"[App] Synced '{acc.Name}' TOTP seed to hardware.");
            ShowNotification($"Synced '{acc.Name}' to hardware key! Finger touch will type this code.");
        }
    }

    private void BtnDeleteAccount_Click(object? sender, RoutedEventArgs e)
    {
        if (sender is Button btn && btn.Tag is TotpAccount acc)
        {
            ShowConfirmation($"Delete '{acc.Name}'?", "Are you sure you want to remove this 2FA account from your vault?", () =>
            {
                _totpAccounts.Remove(acc);
                _vaultService.SaveAccounts(_totpAccounts.ToList());
                ShowNotification($"Removed '{acc.Name}' from vault.");
            });
        }
    }

    #endregion

    #region Fingerprint Studio & 6-Stage Enrollment

    private void BtnStartEnroll_Click(object? sender, RoutedEventArgs e)
    {
        if (!_device.IsConnected) { ShowNotification("Connect device first.", true); return; }

        int slot = (int)(NumSlot.Value ?? 1);
        string actionCode = "WIN";
        if (CboSlotAction.SelectedIndex == 1) actionCode = "TOTP";
        else if (CboSlotAction.SelectedIndex == 2) actionCode = "WIN";
        else if (CboSlotAction.SelectedIndex == 3) actionCode = "LIN";
        else if (CboSlotAction.SelectedIndex == 4) actionCode = "MAC";

        _device.SendCommand($"BIND:{slot}:{actionCode}");
        _device.SendCommand($"ENROLL:{slot}");

        TxtEnrollStageTitle.Text = $"Enrolling Slot #{slot}";
        TxtEnrollStepCounter.Text = "Step 1 / 6";
        ProgEnroll.Value = 1;
        IconEnrollStage.Data = FingerprintGeom;
        IconEnrollStage.Foreground = new SolidColorBrush(Color.Parse("#60CDFF"));
        TxtEnrollInstruction.Text = "Step 1/6: Place center of finger firmly on sensor.";
        TxtEnrollTip.Text = "Make solid contact with the metal bezel ring.";
        BorderEnrollGuide.BorderBrush = new SolidColorBrush(Color.Parse("#0067C0"));

        ShowNotification($"Enrollment started for Slot #{slot}. Follow sensor LED prompts.");
    }

    private void BtnDeleteSlot_Click(object? sender, RoutedEventArgs e)
    {
        if (!_device.IsConnected) { ShowNotification("Connect device first.", true); return; }
        int slot = (int)(NumSlot.Value ?? 1);

        ShowConfirmation($"Delete Fingerprint Slot #{slot}?", $"Are you sure you want to permanently erase the fingerprint stored in slot #{slot}?", () =>
        {
            _device.SendCommand($"DELETE:{slot}");
            AppendConsole($"[App] Sent delete request for slot #{slot}.");
            ShowNotification($"Erased fingerprint slot #{slot} from hardware.");
        });
    }

    private void OnEnrollmentStepReceived(string line)
    {
        Dispatcher.UIThread.Post(() =>
        {
            if (line.Contains("PLACE_FINGER_"))
            {
                var parts = line.Split('_');
                if (parts.Length >= 4 && int.TryParse(parts[2], out int step))
                {
                    ProgEnroll.Value = step;
                    TxtEnrollStepCounter.Text = $"Step {step} / 6";
                    BorderEnrollGuide.BorderBrush = new SolidColorBrush(Color.Parse("#0067C0"));
                    IconEnrollStage.Data = FingerprintGeom;
                    IconEnrollStage.Foreground = new SolidColorBrush(Color.Parse("#60CDFF"));

                    switch (step)
                    {
                        case 1:
                            TxtEnrollInstruction.Text = "Step 1/6: Place center of finger firmly.";
                            TxtEnrollTip.Text = "Make solid contact with the metal bezel ring.";
                            break;
                        case 2:
                            TxtEnrollInstruction.Text = "Step 2/6: Place the tip of your finger.";
                            TxtEnrollTip.Text = "Angle finger forward to capture upper tip.";
                            break;
                        case 3:
                            TxtEnrollInstruction.Text = "Step 3/6: Place the left side edge.";
                            TxtEnrollTip.Text = "Roll finger slightly to the left side edge.";
                            break;
                        case 4:
                            TxtEnrollInstruction.Text = "Step 4/6: Place the right side edge.";
                            TxtEnrollTip.Text = "Roll finger slightly to the right side edge.";
                            break;
                        case 5:
                            TxtEnrollInstruction.Text = "Step 5/6: Place the lower pad of finger.";
                            TxtEnrollTip.Text = "Shift downward towards lower crease.";
                            break;
                        case 6:
                            TxtEnrollInstruction.Text = "Step 6/6: Place center one last time to finalize.";
                            TxtEnrollTip.Text = "Finalizing high-resolution template.";
                            break;
                    }
                }
            }
            else if (line.Contains("LIFT_FINGER"))
            {
                IconEnrollStage.Data = LiftArrowGeom;
                IconEnrollStage.Foreground = new SolidColorBrush(Color.Parse("#F59E0B"));
                TxtEnrollInstruction.Text = "Lift your finger up now.";
                TxtEnrollTip.Text = "Remove finger completely before placing it again.";
                BorderEnrollGuide.BorderBrush = new SolidColorBrush(Color.Parse("#F59E0B"));
            }
            else if (line.Contains("ENROLL_SUCCESS") || line.Contains("Enroll SUCCESS"))
            {
                ProgEnroll.Value = 6;
                TxtEnrollStepCounter.Text = "Completed";
                IconEnrollStage.Data = CheckmarkGeom;
                IconEnrollStage.Foreground = new SolidColorBrush(Color.Parse("#10B981"));
                TxtEnrollInstruction.Text = "Fingerprint enrolled and armed successfully.";
                TxtEnrollTip.Text = "You can now use this finger immediately.";
                BorderEnrollGuide.BorderBrush = new SolidColorBrush(Color.Parse("#10B981"));
                AppendConsole("[Enroll] Fingerprint successfully enrolled!");
                ShowNotification("Fingerprint successfully enrolled and armed!");
            }
            else if (line.Contains("MISMATCH_FAIL") || line.Contains("FAIL") || line.Contains("Enroll FAILED"))
            {
                IconEnrollStage.Data = AlertGeom;
                IconEnrollStage.Foreground = new SolidColorBrush(Color.Parse("#EF4444"));
                TxtEnrollInstruction.Text = "Enrollment failed. Scans did not match.";
                TxtEnrollTip.Text = "Click 'Start 6-Stage Enrollment' to try again.";
                BorderEnrollGuide.BorderBrush = new SolidColorBrush(Color.Parse("#EF4444"));
                AppendConsole("[Enroll] Enrollment failed. Please retry.");
                ShowNotification("Enrollment failed. Scans did not match.", true);
            }
        });
    }

    #endregion

    #region Device & Console

    private void BtnStatus_Click(object? sender, RoutedEventArgs e)
    {
        if (!_device.IsConnected) { ShowNotification("Connect device first.", true); return; }
        _device.SendCommand("STATUS");
        ShowNotification("Requested hardware diagnostics.");
    }

    private void BtnWipe_Click(object? sender, RoutedEventArgs e)
    {
        if (!_device.IsConnected) { ShowNotification("Connect device first.", true); return; }

        ShowConfirmation("Factory Wipe Sensor?", "WARNING: This will permanently delete all 40 fingerprint templates from the hardware sensor. This cannot be undone.", () =>
        {
            _device.SendCommand("WIPE");
            AppendConsole("[App] Requested full database wipe.");
            ShowNotification("Sent factory wipe command to hardware.");
        });
    }

    private void BtnClearConsole_Click(object? sender, RoutedEventArgs e)
    {
        TxtConsole.Text = string.Empty;
        ShowNotification("Console cleared.");
    }

    private void OnDeviceLineReceived(string line)
    {
        Dispatcher.UIThread.Post(() =>
        {
            AppendConsole($"[Device] {line}");

            // Handle hardware confirmations
            if (line.StartsWith("DELETE:SUCCESS"))
            {
                ShowNotification("Hardware confirmed slot deletion.");
            }
            else if (line.StartsWith("WIPE:SUCCESS"))
            {
                ShowNotification("Hardware sensor wiped successfully! All slots cleared.");
            }
            else if (line.StartsWith("SSH_GEN:SUCCESS"))
            {
                ShowNotification("Hardware SSH keypair generated! Reading public key...");
                _device.SendCommand("CMD:SSH_PUB");
            }
            else if (line.StartsWith("SSH_GEN:FAILED"))
            {
                ShowNotification("Hardware SSH key generation failed.", true);
            }
        });
    }

    private void AppendConsole(string text)
    {
        string ts = DateTime.Now.ToString("HH:mm:ss");
        TxtConsole.Text += $"[{ts}] {text}\n";
        TxtConsole.CaretIndex = TxtConsole.Text?.Length ?? 0;
    }

    #endregion
}
