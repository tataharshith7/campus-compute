#!/bin/bash
# Quick checks for CampusCompute. Run via 'make test' from the project folder.
C=./campuscompute
OUT=$(mktemp)
pass=0; fail=0

# expect <description> <expected exit code> <command...>
expect() {
    desc=$1; want=$2; shift 2
    "$@" >"$OUT" 2>&1; rc=$?
    if [ "$rc" -eq "$want" ]; then pass=$((pass+1)); printf '  [PASS] %s\n' "$desc"
    else fail=$((fail+1)); printf '  [FAIL] %s (exit %s, wanted %s)\n' "$desc" "$rc" "$want"; fi
}
# has <description> <text that must appear in the last command's output>
has() {
    if grep -q -- "$2" "$OUT"; then pass=$((pass+1)); printf '  [PASS] %s\n' "$1"
    else fail=$((fail+1)); printf '  [FAIL] %s (missing: %s)\n' "$1" "$2"; fi
}

echo "Good input:"
expect "help"                      0 $C help
expect "jobs"                      0 $C jobs
expect "fcfs"                      0 $C fcfs;     has "FCFS avg waiting 7.40" "7.40"
expect "rr quantum 2"              0 $C rr data/jobs.txt 2;  has "RR avg waiting 9.40" "9.40"
expect "priority"                  0 $C priority
expect "compare"                   0 $C compare
expect "idle gaps + tie file"      0 $C fcfs data/jobs_idle_tie.txt; has "idle shown" "idle"
expect "banker safe"               0 $C banker data/banker_safe.txt;   has "safe sequence" "P1 P3 P4 P0 P2"
expect "banker unsafe"             0 $C banker data/banker_unsafe.txt; has "UNSAFE reported" "UNSAFE"
expect "logger 3 5"                0 $C logger 3 5;  has "logger PASS" "PASS"
expect "ipc"                       0 $C ipc;         has "ipc PASS" "PASS"
expect "workers normal"            0 $C workers normal
expect "workers fail"              0 $C workers fail; has "simulated failure" "SIMULATED"
expect "rlimit"                    0 $C rlimit
expect "status"                    0 $C status
expect "cleanup"                   0 $C cleanup

echo "Bad input (must exit 2):"
expect "no command"                2 $C
expect "unknown command"           2 $C foo
expect "rr quantum abc"            2 $C rr data/jobs.txt abc
expect "rr quantum 0"              2 $C rr data/jobs.txt 0
expect "missing job file"          2 $C jobs nofile
expect "duplicate job id"          2 $C jobs data/bad_jobs_duplicate.txt
expect "malformed job line"        2 $C jobs data/bad_jobs_malformed.txt
expect "banker without file"       2 $C banker
expect "banker invalid state"      2 $C banker data/bad_banker.txt
expect "too many arguments"        2 $C fcfs a b

echo "Full demo and leftovers:"
expect "demo"                      0 $C demo;  has "all demo checks" "12 of 12"
if ls /dev/shm | grep -q campuscompute; then fail=$((fail+1)); echo "  [FAIL] leftover objects in /dev/shm"
else pass=$((pass+1)); echo "  [PASS] no campuscompute objects in /dev/shm"; fi
if pgrep -x campuscompute >/dev/null; then fail=$((fail+1)); echo "  [FAIL] leftover processes"
else pass=$((pass+1)); echo "  [PASS] no leftover processes"; fi

echo; echo "Result: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
