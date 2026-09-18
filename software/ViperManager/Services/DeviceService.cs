using System;
using System.Collections.Generic;
using System.IO.Ports;
using System.Linq;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using ViperManager.Models;

namespace ViperManager.Services;

public class DeviceService : IDisposable
{
    private SerialPort? _serial;
    private Socket? _svcSocket;
    private CancellationTokenSource? _cts;
    private readonly object _lock = new();

    public bool IsConnected => _serial?.IsOpen ?? false;
    public bool IsBluetoothConnected => false;
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
                        DisplayName = "Viper Biometric Key",
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
                        DisplayName = "Viper Biometric Key",
                        IsCompatible = true,
                        IsBluetooth = false
                    });
                }
            }
        }

        if (list.Count > 1)
        {
            for (int i = 0; i < list.Count; i++)
                list[i].DisplayName = $"Viper Biometric Key #{i + 1}";
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
        return await ConnectSerialAsync(dev.PortName);
    }

    public async Task<bool> ConnectAsync(string portName)
    {
        if (string.IsNullOrWhiteSpace(portName)) return false;
        return await ConnectSerialAsync(portName);
    }

    private async Task<bool> ConnectSerialAsync(string portName)
    {
        return await Task.Run(() =>
        {
            lock (_lock)
            {
                DisconnectInternal();

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
                        Thread.Sleep(500); 
                        IsServiceLinked = true;
                    }
                    catch
                    {
                        _svcSocket?.Dispose();
                        _svcSocket = null;
                        IsServiceLinked = false;
                    }
                }

                try
                {
                    _serial = new SerialPort(portName, 115200, Parity.None, 8, StopBits.One)
                    {
                        ReadTimeout = 200,
                        WriteTimeout = 1000,
                        DtrEnable = true, 
                        RtsEnable = true
                    };
                    _serial.Open();
                    CurrentPort = portName;

                    _cts = new CancellationTokenSource();
                    Task.Run(() => ReadLoop(_cts.Token));

                    ConnectionStateChanged?.Invoke(true, portName);

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
