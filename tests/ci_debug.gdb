set architecture arm
set logging enabled on
# 🚨 ИСПРАВЛЕНИЕ: Используем IPv4 вместо localhost
target remote 127.0.0.1:3333

printf "\n================ [CI DEBUG DUMP] ================\n"
printf "g_stats:\n"
print g_stats

printf "\ng_read_pos:\n"
print g_read_pos

printf "\nhuart1.gState: %d\n", huart1.gState
printf "huart1.RxState: %d\n", huart1.RxState
printf "huart1.ErrorCode: %d\n", huart1.ErrorCode
printf "=================================================\n"
quit
