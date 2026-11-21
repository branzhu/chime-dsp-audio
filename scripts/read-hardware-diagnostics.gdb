set pagination off
set print pretty on
target remote localhost:3333
monitor reset run
monitor sleep 10000
monitor halt
printf "Diagnostic complete must be 1; result 0 means communication PASS.\n"
print hardware_diagnostic_report
detach
quit
