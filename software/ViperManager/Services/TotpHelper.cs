using System;
using System.Security.Cryptography;

namespace ViperManager.Services;

public static class TotpHelper
{
    public static (string code, int secondsLeft) GenerateLiveCode(string base32Secret)
    {
        if (string.IsNullOrWhiteSpace(base32Secret))
            return ("------", 0);

        byte[] key;
        try
        {
            key = Base32Decode(base32Secret.Trim());
        }
        catch
        {
            return ("INVALID", 0);
        }

        if (key.Length == 0) return ("------", 0);

        long unixTime = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
        long counter = unixTime / 30;
        int secondsLeft = (int)(30 - (unixTime % 30));

        byte[] counterBytes = BitConverter.GetBytes(counter);
        if (BitConverter.IsLittleEndian)
        {
            Array.Reverse(counterBytes);
        }

        using var hmac = new HMACSHA1(key);
        byte[] hash = hmac.ComputeHash(counterBytes);

        int offset = hash[^1] & 0x0F;
        int binaryCode = ((hash[offset] & 0x7F) << 24) |
                         ((hash[offset + 1] & 0xFF) << 16) |
                         ((hash[offset + 2] & 0xFF) << 8) |
                         (hash[offset + 3] & 0xFF);

        int codeInt = binaryCode % 1000000;
        return (codeInt.ToString("D6"), secondsLeft);
    }

    public static byte[] Base32Decode(string base32)
    {
        const string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
        base32 = base32.ToUpperInvariant().Replace(" ", "").Replace("-", "").TrimEnd('=');
        if (base32.Length == 0) return Array.Empty<byte>();

        var bytes = new System.Collections.Generic.List<byte>();
        int buffer = 0;
        int bitsLeft = 0;

        foreach (char c in base32)
        {
            int val = alphabet.IndexOf(c);
            if (val < 0) continue;

            buffer = (buffer << 5) | val;
            bitsLeft += 5;

            if (bitsLeft >= 8)
            {
                bytes.Add((byte)((buffer >> (bitsLeft - 8)) & 0xFF));
                bitsLeft -= 8;
                buffer &= (1 << bitsLeft) - 1;
            }
        }

        return bytes.ToArray();
    }
}
