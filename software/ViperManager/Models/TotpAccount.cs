using System;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace ViperManager.Models;

public class TotpAccount : INotifyPropertyChanged
{
    private string _liveCode = "------";
    private int _secondsLeft = 30;

    public string Id { get; set; } = Guid.NewGuid().ToString("N");
    public string Name { get; set; } = string.Empty;
    public string Secret { get; set; } = string.Empty;

    public string MaskedSecret
    {
        get
        {
            if (string.IsNullOrEmpty(Secret) || Secret.Length < 8) return "••••••••";
            return $"{Secret.Substring(0, 4)}••••{Secret.Substring(Secret.Length - 4)}";
        }
    }

    public string LiveCode
    {
        get => _liveCode;
        set
        {
            if (_liveCode != value)
            {
                _liveCode = value;
                OnPropertyChanged();
            }
        }
    }

    public int SecondsLeft
    {
        get => _secondsLeft;
        set
        {
            if (_secondsLeft != value)
            {
                _secondsLeft = value;
                OnPropertyChanged();
                OnPropertyChanged(nameof(CountdownText));
            }
        }
    }

    public string CountdownText => $"{SecondsLeft}s";

    public event PropertyChangedEventHandler? PropertyChanged;
    protected void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}
