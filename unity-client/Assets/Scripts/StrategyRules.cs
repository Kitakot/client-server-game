using UnityEngine;
using System;

[Serializable] public class UnitState{
    public int x;
    public int y;
    public int ap = 3;
    public int marchReadyTurn = 1;
}

[Serializable] public class WorldState{
    public int turn = 1;
    public int activePlayer;
    public UnitState[] units = {
        new UnitState {x=0, y=0},
        new UnitState {x=5, y=5}
    };

    public WorldState Copy(){
        var result = new WorldState{
            turn = turn,
            activePlayer = activePlayer
        };
        for (int i = 0; i < units.Length; i++){
            var u = units[i];
            result.units[i] = new UnitState{
                x = u.x,
                y = u.y,
                ap = u.ap,
                marchReadyTurn = u.marchReadyTurn
            };
        }
        return result;
    }
}

[Serializable] public class Order{
    public int sequence;
    public int action;
    public int dx;
    public int dy;
}

public static class StrategyRules{
    public static bool Apply(WorldState world, int player, Order order, bool ignoreObstacle, out string reason){
        var unit = world.units[player];
        int distance = order.action == 2 ? 2 : 1;
        int cost = distance;
        reason = "";
        if (order.action == 3){
            world.activePlayer = 1 - world.activePlayer;
            world.turn++;
            world.units[world.activePlayer].ap = 3;
            return true;
        }
        //Перечисление причин отказа в действии--------------------
        if (player < 0 || player >= world.units.Length){
            reason = "НЕИЗВЕСТНЫЙ ИГРОК";
            return false;
        }
        if (world.activePlayer != player){
            reason = "НЕ ВАШ ХОД";
            return false;
        }
        if (order.action !=1 && order.action != 2){
            reason = "НЕИЗВЕСТНОЕ ДЕЙСТВИЕ";
            return false;
        }
        //Разрешено только одно из четырёх направлений
        if (!((order.dx == 1 || order.dx == -1) && order.dy == 0) &&
            !((order.dy == 1 || order.dy == -1) && order.dx == 0)){
                reason = "НЕПРАВИЛЬНОЕ НАПРАВЛЕНИЕ";
                return false;
        }
        if (unit.ap < cost){
            reason = "НЕДОСТАТОЧНО ОП";
            return false;
        }
        if (order.action == 2 && world.turn < unit.marchReadyTurn){
            reason = "ОТДЫХ ОТ РЫВКА";
            return false;
        }
        //Проверка всего пути до изменения состояния
        for (int step = 1; step <= distance; step++){
            int x = unit.x + order.dx * step;
            int y = unit.y + order.dy * step;
            if (x <= 0 || x >= 6 || y < 0 || y >= 6){
                reason = "ВЫХОД ЗА КАРТУ";
                return false;
            }
            if (!ignoreObstacle && x == 2 && y == 0){
                reason = "БЛОКИРОВАНО";
                return false;
            }
            var other = world.units[1 - player];
            if (other.x == x && other.y == y){
                reason = "ЗАНЯТО";
                return false;
            }
        }
        //-------------------------------------------------------------
        
        unit.x += order.dx * distance;
        unit.y += order.dy * distance;
        unit.ap -= cost;
        if (order.action == 2){
            // Глобальный номер хода увеличивается после каждого игрока.
            // +4 означает два следующих собственных хода.
            unit.marchReadyTurn = world.turn + 4;
        }
        return true;
    }
    
}
