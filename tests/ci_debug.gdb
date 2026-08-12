set architecture arm
set logging enabled on
target remote 127.0.0.1:3333

printf "\n================ [CI DEBUG DUMP] ================\n"
info registers pc
bt

printf "\ng_stats:\n"
print g_stats

printf "\n=================================================\n"
quit
