using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using UnityEngine;

public sealed class PredictionClient : MonoBehaviour{
    public string serverIp = "127.0.0.1";
    public int serverPort = 54010;
    public bool predictionEnabled = true;
    // Намеренно неправильная клиентская проверка.
    // На сервер этот флаг не передаётся.
    public bool debugIgnoreObstacle;
    private UdpClient socket;
    private IPEndPoint server;
    private readonly List<TurnInput> pending = new List<TurnInput>();
    private PredictionWorld authoritative = new PredictionWorld();
    private PredictionWorld displayed = new PredictionWorld();
    private int player = -1;
    private uint nextInputId;
    private uint lastRevision;
    private uint lastProcessed;
    private double nextRetry;
    private double lastSnapshotAt;
    private bool started;
    private string status = "Not connected";

    private static double Now{
        get {return Time.realtimeSinceStartupAsDouble;}
    }

    private void Awake(){
        Application.runInBackground = true;
    }

    private void Connect(){
        try{
            server = new IPEndPoint(IPAddress.Parse(serverIp), serverPort);
            socket = new UdpClient(new IPEndPoint(IPAddress.Any, 0));
            socket.Client.Blocking = false;
            started = true;
            nextRetry = 0;
            lastSnapshotAt = Now;
            status = "Connecting";
        } catch (Exception error){
            socket?.Close();
            socket = null;
            status = error.Message;
        }
    }

    private void Send(byte[] bytes){
        try{
            socket.Send(bytes, bytes.Length, server);
        } catch (SocketException error){
            status = "Send error: " + error.SocketErrorCode;
        }
    }

    private void Update(){
        if (!started) return;
        ReceiveSnapshots();
        if (Now >= nextRetry){
            nextRetry = Now + 0.2;
            if (player < 0){
                Send(PredictionCodec.Hello());
            } else{
                // Повторы сохраняют тот же игровой номер приказа.
                foreach (TurnInput input in pending)
                    Send(PredictionCodec.EncodeInput(input));
            }
        }
    }

    private void ReceiveSnapshots(){
        try{
            for (int count = 0; count < 128 && socket.Available > 0; count++){
                IPEndPoint from = new IPEndPoint(IPAddress.Any, 0);
                byte[] bytes = socket.Receive(ref from);
                if (!from.Equals(server)) continue;
                WorldSnapshot snapshot;
                if (PredictionCodec.TryDecodeSnapshot(bytes, out snapshot)){
                    ApplySnapshot(snapshot);
                }
            }
        } catch(SocketException error){
            if (error.SocketErrorCode != SocketError.WouldBlock)
                status = "Receive error: " + error.SocketErrorCode;
        }
    }

    private void ApplySnapshot(WorldSnapshot snapshot){
        // Для короткой учебной сессии номера не переполняются.
        if (snapshot.Revision <= lastRevision) return;
        if (player >= 0 && snapshot.Player != player) return;
        if (snapshot.ProcessedInput < lastProcessed || snapshot.ProcessedInput > nextInputId) return;
        lastSnapshotAt = Now;
        lastRevision = snapshot.Revision;
        bool wasConnected = player >= 0;
        player = snapshot.Player;
        PredictionUnit oldView = displayed.Units[player].Copy();
        bool acknowledged = snapshot.ProcessedInput > lastProcessed;
        foreach (TurnInput input in pending){
            if (input.Id <= snapshot.ProcessedInput){
                Debug.Log(
                    $"PROCESSED input={input.Id} " +
                    $"elapsedMs={(Now - input.SentAt) * 1000:F0}");
            }
        }
        pending.RemoveAll(input => input.Id <= snapshot.ProcessedInput);
        lastProcessed = snapshot.ProcessedInput;
        // Авторитетная база.
        authoritative = snapshot.World.Copy();
        // Восстанавливаем представление из базы и оставшихся приказов.
        RebuildPrediction();
        status = snapshot.Result.ToString();
        PredictionUnit newView = displayed.Units[player];
        if (wasConnected && acknowledged && predictionEnabled &&
            (oldView.X != newView.X ||
             oldView.Y != newView.Y ||
             oldView.Ap != newView.Ap)){
            Debug.Log(
                $"CORRECTION input={snapshot.ProcessedInput} " +
                $"position=({oldView.X},{oldView.Y})->" +
                $"({newView.X},{newView.Y}) " +
                $"AP={oldView.Ap}->{newView.Ap} " +
                $"result={snapshot.Result}");
        }
    }

    private void RebuildPrediction(){
        displayed = authoritative.Copy();
        if (!predictionEnabled || player < 0) return;
        foreach (TurnInput input in pending){
            TurnResult result = PredictionRules.Apply(
                displayed,
                player,
                input,
                debugIgnoreObstacle);
            Debug.Log($"REPLAY input={input.Id} result={result}");
        }
    }

    private void Submit(TurnAction action, int dx, int dy){
        if (player < 0 ||
            pending.Count >= 32 ||
            nextInputId == uint.MaxValue ||
            Now - lastSnapshotAt > 5)
            return;
        var input = new TurnInput{
            Id = ++nextInputId,
            Turn = displayed.Turn,
            Action = action,
            Dx = dx,
            Dy = dy,
            SentAt = Now
        };
        pending.Add(input);
        Debug.Log(
            $"INPUT id={input.Id} turn={input.Turn} " +
            $"action={input.Action} time={Now:F3}");

        if (predictionEnabled){
            TurnResult result = PredictionRules.Apply(
                displayed,
                player,
                input,
                debugIgnoreObstacle);
            Debug.Log(
                $"PREDICT input={input.Id} result={result} " +
                $"time={Now:F3}");
        }
        Send(PredictionCodec.EncodeInput(input));
    }

    private void OnGUI(){
        GUILayout.BeginArea(new Rect(10, 10, 650, 700));
        if (!started){
            GUILayout.Label($"Server: {serverIp}:{serverPort}");
            if (GUILayout.Button("Connect"))
                Connect();
            GUILayout.Label(status);
            GUILayout.EndArea();
            return;
        }
        GUILayout.Label($"Player: {player}");
        GUILayout.Label(
            $"Turn: {displayed.Turn}; " +
            $"active player: {displayed.ActivePlayer}");
        GUILayout.Label(
            $"Processed: {lastProcessed}; pending: {pending.Count}");
        GUILayout.Label($"Result: {status}");
        if (Now - lastSnapshotAt > 5)
            GUILayout.Label("No server snapshots: input suspended");
        // Переключаем режим только при пустой очереди.
        if (pending.Count == 0){
            bool newPrediction = GUILayout.Toggle(
                predictionEnabled, "Prediction");
            bool newDebug = GUILayout.Toggle(
                debugIgnoreObstacle,
                "DEBUG: ignore obstacle locally");
            if (newPrediction != predictionEnabled ||
                newDebug != debugIgnoreObstacle){
                predictionEnabled = newPrediction;
                debugIgnoreObstacle = newDebug;
                RebuildPrediction();
            }
        }
        for (int y = 5; y >= 0; y--){
            string row = "";
            for (int x = 0; x < 6; x++){
                string cell = x == 2 && y == 0 ? "[#]" : "[ ]";
                for (int i = 0; i < 2; i++){
                    PredictionUnit unit = displayed.Units[i];
                    if (unit.X == x && unit.Y == y)
                        cell = "[" + i + "]";
                }
                row += cell;
            }
            GUILayout.Label(row);
        }
        for (int i = 0; i < 2; i++){
            PredictionUnit unit = displayed.Units[i];
            GUILayout.Label(
                $"Unit {i}: ({unit.X},{unit.Y}), AP={unit.Ap}, " +
                $"marchReadyTurn={unit.MarchReadyTurn}");
        }
        if (GUILayout.Button("Step right"))
            Submit(TurnAction.Step, 1, 0);
        if (GUILayout.Button("Step left"))
            Submit(TurnAction.Step, -1, 0);
        if (GUILayout.Button("Step up"))
            Submit(TurnAction.Step, 0, 1);
        if (GUILayout.Button("Step down"))
            Submit(TurnAction.Step, 0, -1);
        if (GUILayout.Button("March right"))
            Submit(TurnAction.March, 1, 0);
        if (GUILayout.Button("March up"))
            Submit(TurnAction.March, 0, 1);
        if (GUILayout.Button("End turn"))
            Submit(TurnAction.EndTurn, 0, 0);
        GUILayout.EndArea();
    }
    private void OnDestroy(){
        socket?.Close();
    }    
}
