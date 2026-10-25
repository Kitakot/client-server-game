# Запускает шесть серий ПР №3. Clumsy управляется только через GUI, поэтому
# перед каждой серией скрипт просит включить нужные настройки.
# Запуск из корня репозитория: .\tools\run_reliability.ps1
param(
    [string]$Client = ".\build\Debug\reliable_client.exe",
    [int]$Count = 50,
    [int]$IntervalMs = 200,
    [int]$MaxAttempts = 5
)

$series = [ordered]@{
    "baseline"         = "Clumsy выключен"
    "loss_5"           = "Drop 5%"
    "loss_10"          = "Drop 10%"
    "loss_20"          = "Drop 20%"
    "delay_100_loss_5" = "Lag 100 мс + Drop 5%"
    "jitter_loss_10"   = "Lag 50 мс + Throttle 100 мс 50% + Drop 10%"
}

if (Test-Path docs\reliability_samples.csv) {
    Write-Warning "docs\reliability_samples.csv уже существует, серии будут дописаны в конец"
}

Write-Host "Фильтр Clumsy: outbound and (udp.DstPort == 54000 or udp.SrcPort == 54000)"
foreach ($name in $series.Keys) {
    Read-Host "Серия ${name}: перезапустите server.exe, в Clumsy включите '$($series[$name])' и нажмите Enter"
    & $Client $name $Count $IntervalMs $MaxAttempts
}