using System;
using System.IO;
using System.Text;

namespace ViperManager.Services;

public static class SshKeyFormatter
{
    /// <summary>
    /// Converts a 65-byte uncompressed ECDSA P-256 public key (hex starting with 04)
    /// into standard OpenSSH format: ecdsa-sha2-nistp256 AAAAE2... comment
    /// </summary>
    public static string FormatOpenSsh(string hexPub, string comment = "viper@hardware-key")
    {
        if (string.IsNullOrWhiteSpace(hexPub)) return string.Empty;
        hexPub = hexPub.Trim();

        byte[] rawPoint;
        try
        {
            rawPoint = Convert.FromHexString(hexPub);
        }
        catch
        {
            return "Invalid hex format";
        }

        if (rawPoint.Length != 65 || rawPoint[0] != 0x04)
        {
            return "Invalid P-256 public point (expected 65 bytes starting with 0x04)";
        }

        using var ms = new MemoryStream();
        using var bw = new BinaryWriter(ms);

        // 1. Key type string "ecdsa-sha2-nistp256"
        WriteString(bw, "ecdsa-sha2-nistp256");

        // 2. Identifier string "nistp256"
        WriteString(bw, "nistp256");

        // 3. Point Q (length prefix + 65 bytes)
        WriteBytes(bw, rawPoint);

        string base64Blob = Convert.ToBase64String(ms.ToArray());
        return $"ecdsa-sha2-nistp256 {base64Blob} {comment}";
    }

    private static void WriteString(BinaryWriter bw, string value)
    {
        byte[] bytes = Encoding.ASCII.GetBytes(value);
        WriteBytes(bw, bytes);
    }

    private static void WriteBytes(BinaryWriter bw, byte[] data)
    {
        // Big-endian 32-bit integer length prefix
        byte[] lenBytes = BitConverter.GetBytes((uint)data.Length);
        if (BitConverter.IsLittleEndian)
        {
            Array.Reverse(lenBytes);
        }
        bw.Write(lenBytes);
        bw.Write(data);
    }
}
