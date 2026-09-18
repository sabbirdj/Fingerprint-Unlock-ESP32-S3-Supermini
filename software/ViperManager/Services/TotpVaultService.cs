using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using ViperManager.Models;

namespace ViperManager.Services;

public class TotpVaultService
{
    private readonly string _filePath;

    public TotpVaultService()
    {
        string appData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        string folder = Path.Combine(appData, "ViperKey");
        if (!Directory.Exists(folder))
        {
            Directory.CreateDirectory(folder);
        }
        _filePath = Path.Combine(folder, "totp_vault.json");
    }

    public List<TotpAccount> LoadAccounts()
    {
        try
        {
            if (!File.Exists(_filePath))
            {
                // Create a default starter account so the UI is immediately populated
                var defaults = new List<TotpAccount>
                {
                    new TotpAccount { Name = "Primary 2FA Account", Secret = "JBSWY3DPEHPK3PXP" }
                };
                SaveAccounts(defaults);
                return defaults;
            }

            string json = File.ReadAllText(_filePath);
            var list = JsonSerializer.Deserialize<List<TotpAccount>>(json);
            return list ?? new List<TotpAccount>();
        }
        catch
        {
            return new List<TotpAccount>();
        }
    }

    public void SaveAccounts(List<TotpAccount> accounts)
    {
        try
        {
            var options = new JsonSerializerOptions { WriteIndented = true };
            string json = JsonSerializer.Serialize(accounts, options);
            File.WriteAllText(_filePath, json);
        }
        catch { }
    }
}
