@echo off
rem Runs the game writing where each frame's time goes to HALO.log, every 10 seconds
rem (debug.frame_budget: the lines that begin "budget:")
set HALO_FRAME_BUDGET=10
halo.exe
