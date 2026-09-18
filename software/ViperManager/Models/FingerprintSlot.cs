namespace ViperManager.Models;

public class FingerprintSlot
{
    public int SlotNumber { get; set; }
    public string DisplayName => $"Slot #{SlotNumber}";
    public string ActionCode { get; set; } = "WIN";
    public string ActionDisplay { get; set; } = "Windows Login";
}
