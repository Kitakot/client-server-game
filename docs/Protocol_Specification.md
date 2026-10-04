OnPingSent ──► Pending ──PONG за ≤1 с──────────────► Received
                  │
                  └─прошло >1 с (Expire)──► Timeout ──PONG всё-таки пришёл──► late_response

PONG на уже отвеченный номер   ──► duplicate_response
PONG на номер, которого нет    ──► unknown_response