namespace ViperManager.Models;

public class SerialDeviceInfo
{
    public string PortName { get; set; } = string.Empty;
    public string DisplayName { get; set; } = string.Empty;
    public string DeviceId { get; set; } = string.Empty;
    public bool IsCompatible { get; set; }
    public bool IsBluetooth { get; set; }
    public ulong BluetoothAddress { get; set; }

    public override string ToString() => DisplayName;
}
