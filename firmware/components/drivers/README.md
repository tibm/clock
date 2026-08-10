# drivers/  — device logic, pure I/O + math

X40Movement Tb6612 Qre1113 Sk6812Chain Tas5760 Mcp23017 Bme688 Tsl2591 Bno085 Lt3652Status

Rule 5: drivers never post events, never log at INFO, never block on a queue.
Depends on: `hal/api`, `core`, `board_cfg`.
