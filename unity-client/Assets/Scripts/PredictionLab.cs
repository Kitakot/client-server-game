using System.Collections.Generic;
using System.Net;
using UnityEngine;

public class PredictionLab : MonoBehaviour{
    public bool serverMode;
    public bool prediction = true;

    // Только для намеренного расхождения на клиенте.
    public bool debugIgnoreObstacle;

    public int delayMs;
    public int jitterMs;
    public float lossPercent;

    private sealed class Peer{
        public IPEndPoint endpoint;
        public int processed;
        public string result = "CONNECTED";
    }

    private readonly Peer[] peers = new Peer[2];
    private readonly List<Order> pending = new();
    private LabUdp net;
    private IPEndPoint serverEndpoint;
    private WorldState authority = new();
    private WorldState view = new();
    private int player = -1;
    private int sequence;
    private int revision;
    private int lastRevision = -1;
    private double nextSend;
    private bool started;
    private string status = "Not connected";
    private double lastInputTime;
    private static double Now => Time.realtimeSinceStartupAsDouble;

    private void Awake(){
        Application.runInBackground = true;
    }

    private void StartNetwork(bool asServer){
        serverMode = asServer;
        net = new LabUdp(serverMode ? 54010 : 0);
        serverEndpoint = new IPEndPoint(IPAddress.Loopback, 54010);
        started = true;
        nextSend = 0;
        status = serverMode ? "Server started" : "Connecting";
    }

    private void Update(){
        if (!started) return;
        net.DelayMs = delayMs;
        net.JitterMs = jitterMs;
        net.LossPercent = lossPercent;
        net.Pump(receive);
        if (Now < nextSend)return;
        // Клиенты повторяют очередь, сервер регулярно присылает мир.
        nextSend = Now + 0.1;
        if (serverMode){
            revision++;
            for (int i = 0; i < peers.Length; i++){
                if (peers[i] == null) continue;
                net.Send(new LabPacket{
                    kind = 2,
                    player = i,
                    revision = revision,
                    processedSequence = peers[i].processed,
                    result = peers[i].result,
                    world = authority.Copy()
                }, peers[i].endpoint);
            }
        } else if (player < 0){
            net.Send(new LabPacket { kind = 0 }, serverEndpoint);
        } else{
            foreach (var order in pending){
                net.Send(new LabPacket{
                    kind = 1,
                    order = order
                }, serverEndpoint);
            }
        }
    }

    private int FindPeer(IPEndPoint endpoint){
        for (int i = 0; i < peers.Length; i++){
            if (peers[i] != null &&
                peers[i].endpoint.Equals(endpoint))
                return i;
        } return -1;
    }

    private void receive(LabPacket packet, IPEndPoint from){
        if (serverMode) ReceiveOnServer(packet, from);
        else ReceiveOnClient(packet, from);
    }

    private void ReceiveOnServer(LabPacket packet, IPEndPoint from){
        int owner = FindPeer(from);
        if (packet.kind == 0 && owner < 0){
            for (int i = 0; i < peers.Length; i++){
                if (peers[i] != null) continue;
                peers[i] = new Peer { endpoint = from };
                Debug.Log($"Connected player {i}");
                break;
            } return;
        }
        if (owner < 0 || packet.kind != 1 || packet.order == null) return;
        var peer = peers[owner];
        var order = packet.order;
        // Дубликаты и пришедшие раньше времени приказы не применяем.
        // Клиент повторит их до подтверждения обработки.
        if (order.sequence != peer.processed + 1) return;
        bool accepted = StrategyRules.Apply(
            authority, owner, order, false, out string reason);
        // Продвигаем номер и при отказе, иначе очередь зависнет.
        peer.processed = order.sequence;
        peer.result = accepted ? "ACCEPTED" : reason;
        Debug.Log(
            $"SERVER player={owner} seq={order.sequence} " +
            $"result={peer.result} turn={authority.turn}");
    }

    private void ReceiveOnClient(LabPacket packet, IPEndPoint from){
        if (!from.Equals(serverEndpoint) ||
            packet.kind != 2 ||
            packet.world == null ||
            packet.world.units == null ||
            packet.world.units.Length != 2 ||
            packet.world.units[0] == null ||
            packet.world.units[1] == null ||
            packet.player < 0 || packet.player > 1 ||
            packet.revision <= lastRevision) return;

        lastRevision = packet.revision;
        player = packet.player;
        int oldX = view.units[player].x;
        int oldY = view.units[player].y;
        bool acknowledgedNewOrder = false;
        for (int i = pending.Count - 1; i >= 0; i--){
            if (pending[i].sequence > packet.processedSequence) continue;
            pending.RemoveAt(i);
            acknowledgedNewOrder = true;
        }
        // Reconciliation: сначала авторитетная база.
        authority = packet.world.Copy();
        view = authority.Copy();
        // Затем replay только ещё не обработанных приказов.
        if (prediction){
            foreach (var order in pending){
                StrategyRules.Apply(
                    view, player, order,
                    debugIgnoreObstacle, out _);
            }
        }
        if (acknowledgedNewOrder){
            status = packet.result;
            Debug.Log(
                $"CLIENT processed={packet.processedSequence} " +
                $"result={packet.result} " +
                $"elapsedMs={(Now - lastInputTime) * 1000:F0}");
            if (prediction &&
                (oldX != view.units[player].x ||
                 oldY != view.units[player].y)){
                Debug.Log(
                    $"RECONCILIATION ({oldX},{oldY}) -> " +
                    $"({view.units[player].x},{view.units[player].y})");
            }
        }
    }

    private void Submit(int action, int dx = 0, int dy = 0){
        if (player < 0 || pending.Count >= 32) return;
        var order = new Order{
            sequence = ++sequence,
            action = action,
            dx = dx,
            dy = dy
        };

        pending.Add(order);
        lastInputTime = Now;
        Debug.Log($"INPUT seq={order.sequence} time={Now:F3}");
        if (prediction){
            bool predicted = StrategyRules.Apply(
                view, player, order,
                debugIgnoreObstacle, out string reason);

            Debug.Log(
                $"PREDICT seq={order.sequence} applied={predicted} " +
                $"reason={reason} time={Now:F3}");
        }
        // Первая отправка — сразу; следующие выполняет Update.
        net.Send(new LabPacket{
            kind = 1,
            order = order
        }, serverEndpoint);
    }

    private void OnGUI(){
        GUILayout.BeginArea(new Rect(10, 10, 650, 700));
        if (!started){
            if (GUILayout.Button("Start server")) StartNetwork(true);
            if (GUILayout.Button("Start client")) StartNetwork(false);
            GUILayout.EndArea();
            return;
        }
        GUILayout.Label(serverMode ? "SERVER" : $"CLIENT player={player}");
        GUILayout.Label($"Delay={delayMs}ms Jitter={jitterMs}ms Loss={lossPercent}%");
        var shown = serverMode ? authority : view;
        GUILayout.Label($"Turn={shown.turn}, activePlayer={shown.activePlayer}");
        GUILayout.Label($"Pending={pending.Count}, status={status}");
        for (int y = 5; y >= 0; y--){
            string row = "";
            for (int x = 0; x < 6; x++){
                string cell = x == 2 && y == 0 ? "[#]" : "[ ]";
                for (int i = 0; i < 2; i++){
                    if (shown.units[i].x == x && shown.units[i].y == y) cell = $"[{i}]";
                }
                row += cell;
            }
            GUILayout.Label(row);
        }
        for (int i = 0; i < 2; i++){
            var unit = shown.units[i];
            GUILayout.Label(
                $"Unit {i}: ({unit.x},{unit.y}), AP={unit.ap}, " +
                $"marchReadyTurn={unit.marchReadyTurn}");
        }
        if (!serverMode){
            // Менять режим разрешаем после завершения очереди.
            if (pending.Count == 0){
                prediction = GUILayout.Toggle(prediction, "Prediction");
                debugIgnoreObstacle = GUILayout.Toggle(
                    debugIgnoreObstacle,
                    "DEBUG: ignore obstacle locally");
            }
            if (GUILayout.Button("Step right")) Submit(1, 1, 0);
            if (GUILayout.Button("Step left")) Submit(1, -1, 0);
            if (GUILayout.Button("Step up")) Submit(1, 0, 1);
            if (GUILayout.Button("Step down")) Submit(1, 0, -1);
            if (GUILayout.Button("March right")) Submit(2, 1, 0);
            if (GUILayout.Button("March up")) Submit(2, 0, 1);
            if (GUILayout.Button("End turn")) Submit(3);
        }
        GUILayout.EndArea();
    }

    private void OnDestroy(){
        net?.Dispose();
    }
}
