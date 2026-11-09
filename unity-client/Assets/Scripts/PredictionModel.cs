using UnityEngine;
using System;
using System.Collections.Generic;

public enum TurnAction : byte{
    Step = 1,
    March = 2,
    EndTurn = 3
}

public enum TurnResult : byte{
    Accepted = 0,
    WrongTurn = 1,
    BadAction = 2,
    BadDirection = 3,
    NotEnoughAp = 4,
    Cooldown = 5,
    Blocked = 6,
    Occupied = 7,
    OutOfMap = 8
}

public sealed class PredictionUnit{
    public int X;
    public int Y;
    public int Ap = 3;
    public int MarchReadyTurn = 1;
    public PredictionUnit Copy(){
        return new PredictionUnit{
            X = X,
            Y = Y,
            Ap = Ap,
            MarchReadyTurn = MarchReadyTurn
        };
    }
}

public sealed class PredictionWorld{
    public int Turn = 1;
    public int ActivePlayer;
    public PredictionUnit[] Units = {
        new PredictionUnit { X = 0, Y = 0 },
        new PredictionUnit { X = 5, Y = 5 }
    };
    public PredictionWorld Copy(){
        return new PredictionWorld{
            Turn = Turn,
            ActivePlayer = ActivePlayer,
            Units = new[] { Units[0].Copy(), Units[1].Copy() }
        };
    }
}

public sealed class TurnInput{
    public uint Id;
    public int Turn;
    public TurnAction Action;
    public int Dx;
    public int Dy;
    // Только локальные данные, в пакет не записываются.
    public double SentAt;
}

public sealed class WorldSnapshot{
    public uint Revision;
    public int Player;
    public uint ProcessedInput;
    public TurnResult Result;
    public PredictionWorld World;
}

public static class PredictionRules{
    public static TurnResult Apply(
        PredictionWorld world,
        int player,
        TurnInput input,
        bool ignoreObstacle){
        if (player < 0 || player >= 2 || world.ActivePlayer != player || input.Turn != world.Turn)
            return TurnResult.WrongTurn;
        if (input.Action == TurnAction.EndTurn){
            world.ActivePlayer = 1 - world.ActivePlayer;
            world.Turn++;
            world.Units[world.ActivePlayer].Ap = 3;
            return TurnResult.Accepted;
        }
        if (input.Action != TurnAction.Step && input.Action != TurnAction.March)
            return TurnResult.BadAction;
        bool horizontal = (input.Dx == -1 || input.Dx == 1) && input.Dy == 0;
        bool vertical = (input.Dy == -1 || input.Dy == 1) && input.Dx == 0;
        if (!horizontal && !vertical) return TurnResult.BadDirection;
        PredictionUnit unit = world.Units[player];
        int distance = input.Action == TurnAction.March ? 2 : 1;
        if (unit.Ap < distance) return TurnResult.NotEnoughAp;
        if (input.Action == TurnAction.March && world.Turn < unit.MarchReadyTurn)
            return TurnResult.Cooldown;
        for (int step = 1; step <= distance; step++){
            int x = unit.X + input.Dx * step;
            int y = unit.Y + input.Dy * step;
            if (x < 0 || x >= 6 || y < 0 || y >= 6) return TurnResult.OutOfMap;
            if (!ignoreObstacle && x == 2 && y == 0) return TurnResult.Blocked;
            PredictionUnit other = world.Units[1 - player];
            if (other.X == x && other.Y == y) return TurnResult.Occupied;
        }
        unit.X += input.Dx * distance;
        unit.Y += input.Dy * distance;
        unit.Ap -= distance;
        if (input.Action == TurnAction.March)
            unit.MarchReadyTurn = world.Turn + 4;
        return TurnResult.Accepted;
    }
}

public static class PredictionCodec{
    private const ushort Version = 2;
    private const int HeaderSize = 8;
    private static void Write16(List<byte> output, ushort value){
        output.Add((byte)(value >> 8));
        output.Add((byte)value);
    }
    private static void Write32(List<byte> output, uint value){
        output.Add((byte)(value >> 24));
        output.Add((byte)(value >> 16));
        output.Add((byte)(value >> 8));
        output.Add((byte)value);
    }
    private static ushort Read16(byte[] data, ref int offset){
        ushort value = (ushort)(
            (data[offset] << 8) | data[offset + 1]);
        offset += 2;
        return value;
    }
    private static uint Read32(byte[] data, ref int offset){
        uint value =
            ((uint)data[offset] << 24) |
            ((uint)data[offset + 1] << 16) |
            ((uint)data[offset + 2] << 8) |
            data[offset + 3];
        offset += 4;
        return value;
    }
    private static byte[] Wrap(byte type, ushort sequence, List<byte> payload){
        var output = new List<byte>(HeaderSize + payload.Count);
        output.Add(type);
        Write16(output, sequence);
        Write16(output, checked((ushort)payload.Count));
        Write16(output, Version);
        output.Add(0); // requiresAck
        output.AddRange(payload);
        return output.ToArray();
    }
    public static byte[] Hello(){
        return Wrap(6, 0, new List<byte>());
    }
    public static byte[] EncodeInput(TurnInput input){
        var payload = new List<byte>(17);
        Write32(payload, input.Id);
        Write32(payload, unchecked((uint)input.Turn));
        payload.Add((byte)input.Action);
        Write32(payload, unchecked((uint)input.Dx));
        Write32(payload, unchecked((uint)input.Dy));
        return Wrap(7, unchecked((ushort)input.Id), payload);
    }
    public static bool TryDecodeSnapshot(byte[] data, out WorldSnapshot snapshot){
        snapshot = null;
        if (data == null || data.Length != HeaderSize + 47) return false;
        int offset = 0;
        byte type = data[offset++];
        ushort sequence = Read16(data, ref offset);
        ushort payloadSize = Read16(data, ref offset);
        ushort version = Read16(data, ref offset);
        byte requiresAck = data[offset++];
        if (type != 8 || payloadSize != 47 || version != Version || requiresAck != 0)
            return false;
        var result = new WorldSnapshot();
        result.Revision = Read32(data, ref offset);
        result.Player = data[offset++];
        result.ProcessedInput = Read32(data, ref offset);
        result.Result = (TurnResult)data[offset++];
        var world = new PredictionWorld();
        world.Turn = unchecked((int)Read32(data, ref offset));
        world.ActivePlayer = data[offset++];
        for (int i = 0; i < 2; i++){
            world.Units[i].X = unchecked((int)Read32(data, ref offset));
            world.Units[i].Y = unchecked((int)Read32(data, ref offset));
            world.Units[i].Ap = unchecked((int)Read32(data, ref offset));
            world.Units[i].MarchReadyTurn = unchecked((int)Read32(data, ref offset));
        }
        if (result.Player > 1 ||
            world.ActivePlayer > 1 ||
            world.Turn < 1 ||
            (byte)result.Result > (byte)TurnResult.OutOfMap ||
            sequence != unchecked((ushort)result.Revision))
            return false;
        foreach (PredictionUnit unit in world.Units){
            if (unit.X < 0 || unit.X >= 6 ||
                unit.Y < 0 || unit.Y >= 6 ||
                unit.Ap < 0 || unit.Ap > 3 ||
                unit.MarchReadyTurn < 1)
                return false;
        }
        result.World = world;
        snapshot = result;
        return offset == data.Length;
    }
}
