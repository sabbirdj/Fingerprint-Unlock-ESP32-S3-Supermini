using System;
using System.Collections.Generic;
using System.IO.Ports;
using System.Linq;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Devices.Enumeration;
using Windows.Storage.Streams;
using ViperManager.Models;

namespace ViperManager.Services;

public class DeviceService : IDisposable
{
    private static readonly Guid GattServiceUuid = Guid.Parse("19b10000-e8f2-537e-4f6c-d104768a1214");
    private static readonly Guid GattVaultCharUuid = Guid.Parse("19b10002-e8f2-537e-4f6c-d104768a1214");
    private static readonly Guid GattEnrollCharUuid = Guid.Parse("19b10003-e8f2-537e-4f6c-d104768a1214");
    private static readonly Guid GattStatusCharUuid = Guid.Parse("19b10004-e8f2-537e-4f6c-d104768a1214");

    private SerialPort? _serial;
    private Socket? _svcSocket;
    private CancellationTokenSource? _cts;
    private readonly object _lock = new();
    private readonly SemaphoreSlim _bleWriteLock = new(1, 1);

    // BLE instances
    private BluetoothLEDevice? _bleDevice;
    private GattDeviceService? _bleService;
    private GattCharacteristic? _bleVaultChar;
    private GattCharacteristic? _bleStatusChar;
    private GattCharacteristic? _bleEnrollChar;
    private bool _isBle;

    public bool IsConnected => _isBle ? (_bleDevice != null && _bleVaultChar != null) : (_serial?.IsOpen ?? false);
    public bool IsBluetoothConnected => _isBle && IsConnected;
    public string? CurrentPort { get; private set; }
    public bool IsServiceLinked { get; private set; }

    public event Action<string>? LineReceived;
    public event Action<bool, string>? ConnectionStateChanged;
    public event Action<string>? EnrollmentStepReceived;
    public event Action<string>? SshPubReceived;
    public event Action<string>? SshSigReceived;

    public async Task<List<SerialDeviceInfo>> GetCompatibleDevicesAsync()
    {
        var list = new List<SerialDeviceInfo>();

        // 1. Scan USB CDC Devices
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
        {
            try
            {
                using var searcher = new System.Management.ManagementObjectSearcher(
                    "SELECT Name, DeviceID FROM Win32_PnPEntity WHERE Name LIKE '%(COM%)'");
                foreach (var obj in searcher.Get())
                {
                    string name = obj["Name"]?.ToString() ?? "";
                    string devId = obj["DeviceID"]?.ToString() ?? "";

                    var match = System.Text.RegularExpressions.Regex.Match(name, @"\((COM\d+)\)");
                    if (!match.Success) continue;
                    string port = match.Groups[1].Value;

                    bool isUsb = devId.StartsWith("USB", StringComparison.OrdinalIgnoreCase);
                    bool isEsp32 = devId.Contains("VID_303A", StringComparison.OrdinalIgnoreCase);
                    bool isSiliconLabs = devId.Contains("VID_10C4", StringComparison.OrdinalIgnoreCase);
                    bool isWch = devId.Contains("VID_1A86", StringComparison.OrdinalIgnoreCase);
                    bool isFtdi = devId.Contains("VID_0403", StringComparison.OrdinalIgnoreCase);

                    if (!isUsb && !isEsp32 && !isSiliconLabs && !isWch && !isFtdi)
                        continue;

                    list.Add(new SerialDeviceInfo
                    {
                        PortName = port,
                        DisplayName = "Viper Biometric Key (USB)",
                        DeviceId = devId,
                        IsCompatible = true,
                        IsBluetooth = false
                    });
                }
            }
            catch { }
        }
        else
        {
            foreach (var p in SerialPort.GetPortNames())
            {
                if (p.Contains("usbmodem", StringComparison.OrdinalIgnoreCase) ||
                    p.Contains("usbserial", StringComparison.OrdinalIgnoreCase) ||
                    p.Contains("ttyACM", StringComparison.OrdinalIgnoreCase) ||
                    p.Contains("ttyUSB", StringComparison.OrdinalIgnoreCase))
                {
                    list.Add(new SerialDeviceInfo
                    {
                        PortName = p,
                        DisplayName = "Viper Biometric Key (USB)",
                        IsCompatible = true,
                        IsBluetooth = false
                    });
                }
            }
        }

        // 2. Scan Bluetooth LE Devices (Windows WinRT)
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
        {
            try
            {
                // A. Check paired / known BLE devices
                string aqs = BluetoothLEDevice.GetDeviceSelector();
                var bleDevices = await DeviceInformation.FindAllAsync(aqs);
                foreach (var d in bleDevices)
                {
                    string dName = d.Name ?? "";
                    if (dName.Contains("Viper", StringComparison.OrdinalIgnoreCase))
                    {
                        list.Add(new SerialDeviceInfo
                        {
                            PortName = d.Id,
                            DisplayName = "Viper Biometric Key (Bluetooth LE)",
                            DeviceId = d.Id,
                            IsCompatible = true,
                            IsBluetooth = true
                        });
                    }
                }

                // B. Active advertisement scan to discover advertising Viper keys nearby
                var discoveredAddresses = new HashSet<ulong>();
                var watcher = new BluetoothLEAdvertisementWatcher
                {
                    ScanningMode = BluetoothLEScanningMode.Active
                };
                watcher.Received += (s, args) =>
                {
                    string advName = args.Advertisement.LocalName ?? "";
                    if (advName.Contains("Viper", StringComparison.OrdinalIgnoreCase))
                    {
                        lock (discoveredAddresses)
                        {
                            discoveredAddresses.Add(args.BluetoothAddress);
                        }
                    }
                };

                try
                {
                    watcher.Start();
                    await Task.Delay(1200);
                    watcher.Stop();
                }
                catch { }

                foreach (var addr in discoveredAddresses)
                {
                    if (!list.Any(x => x.IsBluetooth && x.BluetoothAddress == addr))
                    {
                        list.Add(new SerialDeviceInfo
                        {
                            PortName = $"BLE:{addr:X12}",
                            DisplayName = "Viper Biometric Key (Bluetooth LE)",
                            DeviceId = "",
                            BluetoothAddress = addr,
                            IsCompatible = true,
                            IsBluetooth = true
                        });
                    }
                }
            }
            catch { }
        }

        // Clean numbering if multiple devices of same type exist
        var usbDevs = list.Where(x => !x.IsBluetooth).ToList();
        if (usbDevs.Count > 1)
        {
            for (int i = 0; i < usbDevs.Count; i++)
                usbDevs[i].DisplayName = $"Viper Biometric Key (USB) #{i + 1}";
        }
        var bleDevs = list.Where(x => x.IsBluetooth).ToList();
        if (bleDevs.Count > 1)
        {
            for (int i = 0; i < bleDevs.Count; i++)
                bleDevs[i].DisplayName = $"Viper Biometric Key (Bluetooth LE) #{i + 1}";
        }

        return list;
    }

    public List<SerialDeviceInfo> GetCompatibleDevices()
    {
        return Task.Run(GetCompatibleDevicesAsync).GetAwaiter().GetResult();
    }

    public async Task<bool> ConnectAsync(SerialDeviceInfo dev)
    {
        if (dev == null || !dev.IsCompatible) return false;

        if (dev.IsBluetooth)
        {
            return await ConnectBleAsync(dev);
        }
        else
        {
            return await ConnectSerialAsync(dev.PortName);
        }
    }

    public async Task<bool> ConnectAsync(string portName)
    {
        if (string.IsNullOrWhiteSpace(portName)) return false;

        var dev = new SerialDeviceInfo
        {
            PortName = portName,
            DisplayName = "Viper Biometric Key (USB)",
            IsCompatible = true,
            IsBluetooth = false
        };
        return await ConnectAsync(dev);
    }

    private async Task<bool> ConnectBleAsync(SerialDeviceInfo dev)
    {
        try
        {
            DisconnectInternal();

            BluetoothLEDevice? bleDev = null;
            if (dev.BluetoothAddress > 0)
            {
                bleDev = await BluetoothLEDevice.FromBluetoothAddressAsync(dev.BluetoothAddress);
            }
            else if (!string.IsNullOrEmpty(dev.DeviceId))
            {
                bleDev = await BluetoothLEDevice.FromIdAsync(dev.DeviceId);
            }

            if (bleDev == null)
            {
                ConnectionStateChanged?.Invoke(false, "Could not open Bluetooth device.");
                return false;
            }

            _bleDevice = bleDev;
            _bleDevice.ConnectionStatusChanged += (s, e) =>
            {
                if (s.ConnectionStatus == BluetoothConnectionStatus.Disconnected)
                {
                    Disconnect();
                }
            };

            // Discover GATT services
            var serviceResult = await _bleDevice.GetGattServicesForUuidAsync(GattServiceUuid, BluetoothCacheMode.Uncached);
            if (serviceResult.Status != GattCommunicationStatus.Success || serviceResult.Services.Count == 0)
            {
                serviceResult = await _bleDevice.GetGattServicesForUuidAsync(GattServiceUuid, BluetoothCacheMode.Cached);
            }

            // If not found and pairing is allowed, attempt pairing
            if ((serviceResult.Status != GattCommunicationStatus.Success || serviceResult.Services.Count == 0) &&
                !_bleDevice.DeviceInformation.Pairing.IsPaired && _bleDevice.DeviceInformation.Pairing.CanPair)
            {
                var pairRes = await _bleDevice.DeviceInformation.Pairing.PairAsync(DevicePairingProtectionLevel.None);
                if (pairRes.Status == DevicePairingResultStatus.Paired || pairRes.Status == DevicePairingResultStatus.AlreadyPaired)
                {
                    serviceResult = await _bleDevice.GetGattServicesForUuidAsync(GattServiceUuid, BluetoothCacheMode.Uncached);
                }
            }

            if (serviceResult.Status != GattCommunicationStatus.Success || serviceResult.Services.Count == 0)
            {
                DisconnectInternal();
                ConnectionStateChanged?.Invoke(false, "Viper Key GATT Service not found.");
                return false;
            }

            _bleService = serviceResult.Services[0];

            // Discover characteristics
            var vaultResult = await _bleService.GetCharacteristicsForUuidAsync(GattVaultCharUuid, BluetoothCacheMode.Uncached);
            if (vaultResult.Status != GattCommunicationStatus.Success || vaultResult.Characteristics.Count == 0)
                vaultResult = await _bleService.GetCharacteristicsForUuidAsync(GattVaultCharUuid, BluetoothCacheMode.Cached);

            if (vaultResult.Status != GattCommunicationStatus.Success || vaultResult.Characteristics.Count == 0)
            {
                DisconnectInternal();
                ConnectionStateChanged?.Invoke(false, "Command Characteristic not found.");
                return false;
            }
            _bleVaultChar = vaultResult.Characteristics[0];

            var statusResult = await _bleService.GetCharacteristicsForUuidAsync(GattStatusCharUuid, BluetoothCacheMode.Uncached);
            if (statusResult.Status != GattCommunicationStatus.Success || statusResult.Characteristics.Count == 0)
                statusResult = await _bleService.GetCharacteristicsForUuidAsync(GattStatusCharUuid, BluetoothCacheMode.Cached);

            if (statusResult.Status != GattCommunicationStatus.Success || statusResult.Characteristics.Count == 0)
            {
                DisconnectInternal();
                ConnectionStateChanged?.Invoke(false, "Status Characteristic not found.");
                return false;
            }
            _bleStatusChar = statusResult.Characteristics[0];

            // Subscribe to Status notifications
            _bleStatusChar.ValueChanged += OnBleStatusCharValueChanged;
            try
            {
                await _bleStatusChar.WriteClientCharacteristicConfigurationDescriptorAsync(
                    GattClientCharacteristicConfigurationDescriptorValue.Notify);
            }
            catch { }

            // Also subscribe to Enroll notifications if available
            var enrollResult = await _bleService.GetCharacteristicsForUuidAsync(GattEnrollCharUuid, BluetoothCacheMode.Uncached);
            if (enrollResult.Status == GattCommunicationStatus.Success && enrollResult.Characteristics.Count > 0)
            {
                _bleEnrollChar = enrollResult.Characteristics[0];
                _bleEnrollChar.ValueChanged += OnBleStatusCharValueChanged;
                try
                {
                    await _bleEnrollChar.WriteClientCharacteristicConfigurationDescriptorAsync(
                        GattClientCharacteristicConfigurationDescriptorValue.Notify);
                }
                catch { }
            }

            _isBle = true;
            CurrentPort = dev.DisplayName;

            ConnectionStateChanged?.Invoke(true, dev.DisplayName);

            // Sync Time and Host OS immediately
            SyncTime();
            SyncHostOs();

            return true;
        }
        catch (Exception ex)
        {
            DisconnectInternal();
            ConnectionStateChanged?.Invoke(false, $"BLE Connection error: {ex.Message}");
            return false;
        }
    }

    private void OnBleStatusCharValueChanged(GattCharacteristic sender, GattValueChangedEventArgs args)
    {
        try
        {
            var reader = DataReader.FromBuffer(args.CharacteristicValue);
            byte[] bytes = new byte[reader.UnconsumedBufferLength];
            reader.ReadBytes(bytes);
            string text = Encoding.UTF8.GetString(bytes).Trim();
            if (string.IsNullOrEmpty(text)) return;

            string[] lines = text.Split(new[] { "\r\n", "\r", "\n" }, StringSplitOptions.RemoveEmptyEntries);
            foreach (var rawLine in lines)
            {
                string line = rawLine.Trim();
                if (string.IsNullOrEmpty(line)) continue;
                ProcessIncomingLine(line);
            }
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"[BLE ValueChanged error] {ex.Message}");
        }
    }

    private async Task<bool> ConnectSerialAsync(string portName)
    {
        return await Task.Run(() =>
        {
            lock (_lock)
            {
                DisconnectInternal();

                // 1. On Windows, signal background service to yield COM port
                if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
                {
                    try
                    {
                        _svcSocket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp)
                        {
                            SendTimeout = 1500,
                            ReceiveTimeout = 1500
                        };
                        _svcSocket.Connect("127.0.0.1", 44332);
                        _svcSocket.Send(Encoding.UTF8.GetBytes("OVERRIDE"));
                        Thread.Sleep(500); // Allow service to close serial port
                        IsServiceLinked = true;
                    }
                    catch
                    {
                        _svcSocket?.Dispose();
                        _svcSocket = null;
                        IsServiceLinked = false;
                    }
                }

                // 2. Open serial connection
                try
                {
                    _serial = new SerialPort(portName, 115200, Parity.None, 8, StopBits.One)
                    {
                        ReadTimeout = 200,
                        WriteTimeout = 1000,
                        DtrEnable = true, // Critical for ESP32-S3 CDC USB
                        RtsEnable = true
                    };
                    _serial.Open();
                    CurrentPort = portName;

                    _cts = new CancellationTokenSource();
                    Task.Run(() => ReadLoop(_cts.Token));

                    ConnectionStateChanged?.Invoke(true, portName);

                    // Sync Time and Host OS immediately
                    SyncTime();
                    SyncHostOs();

                    return true;
                }
                catch (Exception ex)
                {
                    DisconnectInternal();
                    ConnectionStateChanged?.Invoke(false, ex.Message);
                    return false;
                }
            }
        });
    }

    public void Disconnect()
    {
        lock (_lock)
        {
            DisconnectInternal();
            ConnectionStateChanged?.Invoke(false, "Disconnected by user.");
        }
    }

    private void DisconnectInternal()
    {
        _cts?.Cancel();
        _cts?.Dispose();
        _cts = null;

        if (_bleStatusChar != null)
        {
            try { _bleStatusChar.ValueChanged -= OnBleStatusCharValueChanged; } catch { }
            _bleStatusChar = null;
        }
        if (_bleEnrollChar != null)
        {
            try { _bleEnrollChar.ValueChanged -= OnBleStatusCharValueChanged; } catch { }
            _bleEnrollChar = null;
        }
        _bleVaultChar = null;

        if (_bleService != null)
        {
            try { _bleService.Dispose(); } catch { }
            _bleService = null;
        }
        if (_bleDevice != null)
        {
            try { _bleDevice.Dispose(); } catch { }
            _bleDevice = null;
        }
        _isBle = false;

        if (_serial != null)
        {
            try
            {
                if (_serial.IsOpen)
                {
                    _serial.Close();
                }
            }
            catch { }
            _serial.Dispose();
            _serial = null;
        }

        // Release socket so background service resumes immediately
        if (_svcSocket != null)
        {
            try
            {
                _svcSocket.Shutdown(SocketShutdown.Both);
                _svcSocket.Close();
            }
            catch { }
            _svcSocket.Dispose();
            _svcSocket = null;
            IsServiceLinked = false;
        }

        CurrentPort = null;
    }

    public bool SendCommand(string command)
    {
        lock (_lock)
        {
            if (_isBle)
            {
                if (_bleVaultChar == null) return false;
                try
                {
                    var writer = new DataWriter();
                    writer.WriteString(command + "\n");
                    var buffer = writer.DetachBuffer();
                    _ = Task.Run(() => SendBleBufferAsync(buffer));
                    return true;
                }
                catch
                {
                    return false;
                }
            }
            else
            {
                if (_serial == null || !_serial.IsOpen) return false;
                try
                {
                    _serial.WriteLine(command);
                    return true;
                }
                catch
                {
                    return false;
                }
            }
        }
    }

    private async Task SendBleBufferAsync(IBuffer buffer)
    {
        await _bleWriteLock.WaitAsync();
        try
        {
            if (_bleVaultChar != null)
            {
                var writeOption = _bleVaultChar.CharacteristicProperties.HasFlag(GattCharacteristicProperties.WriteWithoutResponse)
                    ? GattWriteOption.WriteWithoutResponse
                    : GattWriteOption.WriteWithResponse;
                await _bleVaultChar.WriteValueAsync(buffer, writeOption);
            }
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"[BLE Send Error] {ex.Message}");
        }
        finally
        {
            _bleWriteLock.Release();
        }
    }

    public void SyncTime()
    {
        long unixTime = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
        SendCommand($"CMD:TIME:{unixTime}");
    }

    public void SyncHostOs()
    {
        string osCode = "WIN";
        if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX)) osCode = "MAC";
        else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux)) osCode = "LIN";

        SendCommand($"OS:{osCode}");
    }

    private void ReadLoop(CancellationToken token)
    {
        while (!token.IsCancellationRequested && _serial != null && _serial.IsOpen)
        {
            try
            {
                string line = _serial.ReadLine().Trim();
                if (string.IsNullOrEmpty(line)) continue;

                ProcessIncomingLine(line);
            }
            catch (TimeoutException)
            {
                // Expected timeout when no characters arrived within ReadTimeout
            }
            catch (Exception)
            {
                if (!token.IsCancellationRequested)
                {
                    Disconnect();
                }
                break;
            }
        }
    }

    private void ProcessIncomingLine(string line)
    {
        LineReceived?.Invoke(line);

        if (line.Contains("[Enroll]"))
        {
            EnrollmentStepReceived?.Invoke(line);
        }
        else if (line.StartsWith("SSH_PUB:"))
        {
            SshPubReceived?.Invoke(line.Substring("SSH_PUB:".Length).Trim());
        }
        else if (line.StartsWith("SIG:"))
        {
            SshSigReceived?.Invoke(line.Substring("SIG:".Length).Trim());
        }
    }

    public void Dispose()
    {
        Disconnect();
    }
}
