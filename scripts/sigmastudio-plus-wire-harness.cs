// Loopback-only test fixture. It is not an ESP32 protocol implementation.
using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using AnalogDevices.TypeDefinitions.Events;
using AnalogDevices.Plugins.Communication.TCPIP;

public sealed class SigmaPlusWireHarness : IDisposable
{
    readonly TcpListener listener = new TcpListener(IPAddress.Loopback, 0);
    readonly Thread worker;
    readonly bool reverseAddress;
    readonly bool registerRead;
    readonly List<byte[]> frames = new List<byte[]>();
    readonly List<byte[]> accepted = new List<byte[]>();
    TcpClient client;
    public string ServerError;
    public int Port { get { return ((IPEndPoint)listener.LocalEndpoint).Port; } }
    public SigmaPlusWireHarness(bool reverse) : this(reverse, false) {}
    public SigmaPlusWireHarness(bool reverse, bool registerReadFixture)
    {
        reverseAddress = reverse;
        registerRead = registerReadFixture;
        listener.Start();
        worker = new Thread(Run) { IsBackground = true };
        worker.Start();
    }
    static byte[] Exact(NetworkStream stream, int count)
    {
        var data = new byte[count];
        for (int offset = 0; offset < count;)
        {
            int read = stream.Read(data, offset, count - offset);
            if (read == 0) throw new Exception("EOF");
            offset += read;
        }
        return data;
    }
    void Run()
    {
        try
        {
            client = listener.AcceptTcpClient();
            client.ReceiveTimeout = 6000;
            using (var stream = client.GetStream())
            {
                while (true)
                {
                    byte control = Exact(stream, 1)[0];
                    // Observed native connection prelude, captured only. NO ACK or
                    // guessed semantics. Firmware must reject this unsupported op.
                    if (control == 0x1b)
                    {
                        var prefix = new byte[11];
                        prefix[0] = control;
                        Array.Copy(Exact(stream, 10), 0, prefix, 1, 10);
                        lock (frames) frames.Add(prefix);
                        continue;
                    }
                    if (control != 9 && control != 10) throw new Exception("Unknown fixture control");
                    var header = new byte[14];
                    header[0] = control;
                    Array.Copy(Exact(stream, 13), 0, header, 1, 13);
                    uint total = control == 9
                        ? ((uint)header[3] << 24) | ((uint)header[4] << 16) | ((uint)header[5] << 8) | header[6]
                        : 14;
                    if (total < 14 || total > 65550) throw new Exception("Fixture length limit");
                    var frame = new byte[(int)total];
                    Array.Copy(header, frame, 14);
                    if (total > 14) Array.Copy(Exact(stream, (int)total - 14), 0, frame, 14, (int)total - 14);
                    lock (frames) frames.Add(frame);
                    if (control == 10)
                    {
                        // Controlled 4-byte successful response; no status=1 UI path.
                        byte hi = header[10], lo = header[11];
                        var reply = new byte[] {11,0,0,0,18,header[5],0,0,0,4,
                            reverseAddress ? lo : hi, reverseAddress ? hi : lo,0,0,1,2,3,4};
                        if (registerRead)
                            reply = new byte[] {11,0,0,0,16,header[5],0,0,0,2,hi,lo,0,0,0,0x60};
                        stream.Write(reply, 0, reply.Length);
                    }
                }
            }
        }
        catch (Exception error) { ServerError = error.Message; }
    }
    public void OnAsyncRead(object sender, AsyncReadEventArgs args)
    {
        // The immediate Read return is NOT acceptance: it can contain the input.
        // Acceptance requires the native asynchronous callback to contain the
        // distinct mock data and the original asymmetric address.
        if (args.ReturnPackage != null && (args.ReturnPackage.Address == 0x1234 ||
            (registerRead && args.ReturnPackage.Address == 0xf000)))
            lock (accepted) accepted.Add(new List<byte>(args.ReturnPackage.Values).ToArray());
    }
    public bool HasAcceptedResponse()
    {
        lock (accepted)
            return accepted.Exists(b => b.Length == 4 && b[0] == 1 && b[1] == 2 && b[2] == 3 && b[3] == 4);
    }
    public bool HasAcceptedRegisterResponse()
    {
        lock (accepted)
            return accepted.Exists(b => b.Length == 2 && b[0] == 0 && b[1] == 0x60);
    }
    public byte[][] Snapshot() { lock (frames) return frames.ToArray(); }
    public void Dispose()
    {
        if (client != null) client.Close();
        listener.Stop();
        if (!worker.Join(7000)) throw new Exception("Loopback worker did not stop");
    }
}
