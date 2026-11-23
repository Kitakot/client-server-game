using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using UnityEngine;

public class ReplicationLabClient : MonoBehaviour{
    public string serverIp = "127.0.0.1";
    public int serverPort = 54020;
    public GameObject objectPrefab;

    private UdpClient socket;
    private IPEndPoint server;
    private readonly GameObject[] objects = new GameObject[64];
    private readonly uint[] revisions = new uint[64];
    private readonly int[] xs = new int[64];
    private readonly int[] ys = new int[64];
    private bool connected;
    private double nextHello;
    private int receivedObjects;
    private long receivedBytes;
    private string status = "Not connected";
    private static double Now => Time.realtimeSinceStartupAsDouble;

    private void Awake()
    {
        Application.runInBackground = true;
    }

    private void Connect()
    {
        if (objectPrefab == null)
        {
            status = "Assign Object Prefab in Inspector";
            return;
        }
        try
        {
            server = new IPEndPoint(IPAddress.Parse(serverIp), serverPort);
            socket = new UdpClient(new IPEndPoint(IPAddress.Any, 0));
            socket.Client.Blocking = false;
            connected = true;
            nextHello = 0;
            status = "Waiting for server and second client";
        }
        catch (Exception error)
        {
            socket?.Close();
            socket = null;
            status = error.Message;
        }
    }

    private static ushort Read16(byte[] data, ref int offset)
    {
        ushort value = (ushort)((data[offset] << 8) | data[offset + 1]);
        offset += 2;
        return value;
    }

    private static uint Read32(byte[] data, ref int offset)
    {
        uint value =
            ((uint)data[offset] << 24) |
            ((uint)data[offset + 1] << 16) |
            ((uint)data[offset + 2] << 8) |
            data[offset + 3];
        offset += 4;
        return value;
    }

    private static void Write16(List<byte> bytes, ushort value)
    {
        bytes.Add((byte)(value >> 8));
        bytes.Add((byte)value);
    }

    private static void Write32(List<byte> bytes, uint value)
    {
        bytes.Add((byte)(value >> 24));
        bytes.Add((byte)(value >> 16));
        bytes.Add((byte)(value >> 8));
        bytes.Add((byte)value);
    }

    private static byte[] Packet(byte type, List<byte> payload)
    {
        var bytes = new List<byte>();
        bytes.Add(type);
        Write16(bytes, 0);
        Write16(bytes, (ushort)payload.Count);
        Write16(bytes, 2);
        bytes.Add(0);
        bytes.AddRange(payload);
        return bytes.ToArray();
    }

    private void Send(byte[] bytes)
    {
        try
        {
            socket.Send(bytes, bytes.Length, server);
        }
        catch (SocketException error)
        {
            status = error.SocketErrorCode.ToString();
        }
    }

    private void SendAck(ushort id, uint revision)
    {
        var payload = new List<byte>();
        Write16(payload, id);
        Write32(payload, revision);
        Send(Packet(23, payload));
    }

    private void Update()
    {
        if (!connected) return;
        if (receivedObjects == 0 && Now >= nextHello)
        {
            nextHello = Now + 0.5;
            Send(Packet(20, new List<byte>()));
        }
        try
        {
            for (int count = 0; count < 512 && socket.Available > 0; count++)
            {
                IPEndPoint from = new IPEndPoint(IPAddress.Any, 0);
                byte[] bytes = socket.Receive(ref from);
                if (!from.Equals(server)) continue;
                ReceiveObject(bytes);
            }
        }
        catch (SocketException error)
        {
            if (error.SocketErrorCode != SocketError.WouldBlock)
                status = error.SocketErrorCode.ToString();
        }
    }

    private void ReceiveObject(byte[] bytes)
    {
        if (bytes.Length != 28 && bytes.Length != 17) return;
        int offset = 0;
        byte type = bytes[offset++];
        ushort sequence = Read16(bytes, ref offset);
        ushort payloadSize = Read16(bytes, ref offset);
        ushort version = Read16(bytes, ref offset);
        byte requiresAck = bytes[offset++];

        bool full = type == 21 && payloadSize == 20;
        bool compact = type == 22 && payloadSize == 9;
        if ((!full && !compact) || bytes.Length != 8 + payloadSize ||
            sequence != 0 || version != 2 || requiresAck != 0) return;

        uint id;
        int x;
        int y;
        uint kind;
        uint revision;
        if (compact)
        {
            id = Read16(bytes, ref offset);
            x = bytes[offset++];
            y = bytes[offset++];
            kind = bytes[offset++];
            revision = Read32(bytes, ref offset);
        }
        else
        {
            id = Read32(bytes, ref offset);
            int rawX = unchecked((int)Read32(bytes, ref offset));
            int rawY = unchecked((int)Read32(bytes, ref offset));
            kind = Read32(bytes, ref offset);
            revision = Read32(bytes, ref offset);
            if (rawX % 1000 != 0 || rawY % 1000 != 0) return;
            x = rawX / 1000;
            y = rawY / 1000;
        }

        if (id >= 64 || kind > 1 || revision == 0 ||
            x < 0 || x > 255 || y < 0 || y > 255) return;

        int index = (int)id;
        receivedBytes += bytes.Length;
        if (revision > revisions[index])
        {
            if (objects[index] == null)
            {
                objects[index] = Instantiate(objectPrefab, transform);
                objects[index].name = "Replicated_" + id;
                receivedObjects++;
            }
            revisions[index] = revision;
            xs[index] = x;
            ys[index] = y;
            objects[index].transform.position =
                new Vector3(x * 1.5f, kind == 0 ? 0.5f : 1f, y * 1.5f);
            objects[index].transform.localScale = kind == 0
                ? new Vector3(0.7f, 1f, 0.7f)
                : new Vector3(0.7f, 2f, 0.7f);
            status = compact ? "Compact state received" : "Full state received";
        }
        // Повтор подтверждаем: прежний ACK мог потеряться.
        SendAck((ushort)id, revision);
    }

    private bool FinalStateMatches()
    {
        if (receivedObjects != 64) return false;
        for (int i = 0; i < 64; i++)
        {
            int expectedX = i % 8;
            int expectedY = i / 8;
            if (i >= 56)
                expectedX = (i % 8 + 55) % 16;
            else if (i < 5)
                expectedY++;
            if (xs[i] != expectedX || ys[i] != expectedY) return false;
        }
        return true;
    }

    private void OnGUI()
    {
        GUILayout.BeginArea(new Rect(10, 10, 480, 230));
        GUILayout.Label($"Server: {serverIp}:{serverPort}");
        if (!connected)
        {
            if (GUILayout.Button("Connect replication client")) Connect();
        }
        GUILayout.Label(status);
        GUILayout.Label($"Objects: {receivedObjects}/64");
        GUILayout.Label($"Received application bytes: {receivedBytes}");
        GUILayout.Label($"Final scenario state: {FinalStateMatches()}");
        GUILayout.EndArea();
    }

    private void OnDestroy()
    {
        socket?.Close();
        foreach (GameObject item in objects)
        {
            if (item != null) Destroy(item);
        }
    }
}
