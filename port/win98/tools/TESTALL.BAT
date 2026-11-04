@echo off
rem The quick checks in one go (about 3 minutes; each of the first two ends with a box to click OK): the self test, the imports, and a 30 second run that
rem writes where the time goes. Then run BENCHALL.BAT for the long one. Send back SELFTEST.TXT, import_check.txt,
rem HALO.log and BENCH.TXT.
echo 1/3 self test
halo.exe --selftest
echo 2/3 imports
import_check.exe
echo 3/3 30 second run
set HALO_FRAME_BUDGET=10
set HALO_EXIT_AFTER=30
set HALO_BENCH_LABEL=quick-default
halo.exe --no-setup
echo done. Send back SELFTEST.TXT import_check.txt HALO.log BENCH.TXT
pause
