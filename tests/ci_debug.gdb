_# Подключаемся к GDB-серверу Renode
target remote localhost:3333

echo \n=== [CI DEBUG] ДАМП ВНУТРЕННИХ ПЕРЕМЕННЫХ C++ ===\n

echo --- g_stats (Статистика) ---\n
print g_stats

echo \n--- Состояние кольцевого буфера ---\n
print g_read_pos

echo \n--- Состояние UART1 ---\n
print huart1.gState
print huart1.RxState
print huart1.ErrorCode

echo \n--- Состояние DMA RX & TX ---\n
print huart1.hdmarx->State
print huart1.hdmatx->State

echo \n=== КОНЕЦ ДАМПА ===\n
quit
