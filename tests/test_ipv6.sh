#!/bin/bash
set -e
trap 'kill "${server_pid:-}" "${client_pid:-}" 2>/dev/null || true' EXIT
for client_family in 6 4; do
if [[ "$client_family" == 6 ]]; then
  server_ip="::1"
  client_ip="::1"
else
  server_ip="::ffff:127.0.0.1"
  client_ip="127.0.0.1"
fi
stdbuf -o0 ./twamp-light-server -n 10 --ip 6 -P 4200 -T 40 &>server_output.txt &
server_pid=$!
sleep 1
stdbuf -o0 ./twamp-light-client -i 10 --ip "$client_family" -n 10 -T 184 localhost:4200 &>client_output.txt &
client_pid=$!
wait $client_pid
client_pid=""
wait $server_pid
server_pid=""

# Check if the server output is correct

# Define the expected header
expected_header="Time,IP,Snd#,Rcv#,SndPort,RscPort,FW_TTL,SndTOS,FW_TOS,IntD,FWD,PLEN,"

# Get the actual header
header=$(head -n 1 server_output.txt)

# Compare the headers
if [[ "$header" != "$expected_header" ]]; then
  echo "Header mismatch"
  exit 1
fi

# Use awk to check the field count of each row, excluding the header row
awk -F, 'NR>1 && NF!=12 {print "Field count mismatch on line", NR; exit 1}' server_output.txt

# Use awk to check the IP field of each row, excluding the header row
awk -F, -v ip="$server_ip" 'NR>1 && $2!=ip {print "IP mismatch on line", NR; exit 1}' server_output.txt

awk -F, 'NR>1 && ($7!=255 || $8!=184) {print "Received hop limit or traffic class mismatch on line", NR; exit 1}
         END {if (NR!=11) {print "Server sample count mismatch"; exit 1}}' server_output.txt

# Use awk to check if the Time field is numeric, excluding the header row
awk -F, 'NR>1 && $1 !~ /^[0-9]+$/ {print "Non-numeric Time value on line", NR; exit 1}' server_output.txt

# Use awk to check if the values in the PLEN field are within a specific range, excluding the header row
awk -F, 'NR>1 && ($12 < 40 || $12 > 1500) {print "PLEN value out of range on line", NR; exit 1}' server_output.txt

# Check if the client output is correct

# Define the expected header
expected_header="Time,IP,Snd#,Rcv#,SndPort,RscPort,Sync,FW_TTL,SW_TTL,SndTOS,FW_TOS,SW_TOS,RTT,IntD,FWD,BWD,PLEN"

# Get the actual header
header=$(head -n 1 client_output.txt)

# Compare the headers
if [[ "$header" != "$expected_header" ]]; then
  echo "Header mismatch"
  exit 1
fi

# Validate the number of fields in each row
awk -F, 'NR>1 && NF!=17 {print "Field count mismatch on line", NR; exit 1}' client_output.txt

# Validate the IP field values, excluding the header row
awk -F, -v ip="$client_ip" 'NR>1 && $2!=ip {print "IP mismatch on line", NR; exit 1}' client_output.txt

awk -F, 'NR>1 && ($8!=255 || $9!=255 || $11!=184 || $12!=40) {print "Reflected hop limit or traffic class mismatch on line", NR; exit 1}
         END {if (NR!=11) {print "Client sample count mismatch"; exit 1}}' client_output.txt

# Check if the Time field is numeric, excluding the header row
awk -F, 'NR>1 && $1 !~ /^[0-9]+$/ {print "Non-numeric Time value on line", NR; exit 1}' client_output.txt

# Validate the range of values in the PLEN field, excluding the header row
awk -F, 'NR>1 && ($17 < 40 || $17 > 1500) {print "PLEN value out of range on line", NR; exit 1}' client_output.txt
done
