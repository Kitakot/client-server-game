using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;
using UnityEngine;

[Serializable] public class LabPacket{
    public int version = 1;
    public int kind;
    public int player = -1;
    public int revision;
    public int processedSequence;
    public string result;
    public Order order;
    public WorldState world;
}

public sealed class LabUdp : IDisposable{
    private sealed class DelayedPacket{
        public double due;
        public byte[] bytes;
        public IPEndPoint target;
    }
    private readonly UdpClient socket;
    private readonly List<DelayedPacket> outgoing = new();
    private readonly System.Random random = new();

    public int DelayMs;
    public int JitterMs;
    public float LossPercent;
    private static double Now => Time.realtimeSinceStartupAsDouble;
    public LabUdp(int localPort){
        socket = new UdpClient(new IPEndPoint(IPAddress.Loopback, localPort));
        socket.Client.Blocking = false;
    }
    public void Send(LabPacket packet, IPEndPoint target){
        if (random.NextDouble() * 100 < LossPercent) return;
        int jitter = JitterMs > 0 ? random.Next(-JitterMs, JitterMs + 1) : 0;
        outgoing.Add(new DelayedPacket{
            due = Now + Math.Max(0, DelayMs + jitter) / 100.0,
            bytes = Encoding.UTF8.GetBytes(JsonUtility.ToJson(packet)),
            target = target
        });
    }
    public void Pump(Action<LabPacket, IPEndPoint> receive){
        for (int i = outgoing.Count - 1; i >= 0; i--){
            if (outgoing[i].due > Now) continue;
            var item = outgoing[i];
            socket.Send(item.bytes, item.bytes.Length, item.target);
            outgoing.RemoveAt(i);
        }
        //Ограничение объёма обработки за один кадр
        for (int count = 0; count < 128 && socket.Available > 0; count++){
            IPEndPoint from = new(IPAddress.Any, 0);
            byte[] bytes = socket.Receive(ref from);
            if (bytes.Length > 4096) continue;
        LabPacket packet;
        try{
            packet = JsonUtility.FromJson<LabPacket>(Encoding.UTF8.GetString(bytes));
        } catch (ArgumentException){
            continue;
        }
        if (packet != null && packet.version == 1) receive(packet, from);
        }
    }
    public void Dispose(){
        socket.Dispose();
    }
}
