#!/bin/bash
# crop.sh X_MM Y_MM W_MM H_MM OUT.png [DPI] — render a region of the schematic
# PDF (A1 assumed at origin 0,0) to PNG for visual inspection.
set -e
SCRATCH="/private/tmp/claude-501/-Users-tibo-Developer-PRIVATE-clock/9a0b3077-20bd-4646-a449-dbf246521d91/scratchpad"
PDF="$SCRATCH/clock2.pdf"
DPI=${6:-300}
X=$(python3 -c "print(int($1/25.4*$DPI))")
Y=$(python3 -c "print(int($2/25.4*$DPI))")
W=$(python3 -c "print(int($3/25.4*$DPI))")
H=$(python3 -c "print(int($4/25.4*$DPI))")
pdftoppm -png -r "$DPI" -x "$X" -y "$Y" -W "$W" -H "$H" -singlefile "$PDF" "${5%.png}"
echo "wrote ${5%.png}.png"
