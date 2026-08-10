# domain/  — pure product logic, zero IDF

Alarm scheduler (DST/TZ), hand math (`HandAngle` wrap, shortest path, backlash), sunrise
curve, DSP biquad + limiter, gamma. Everything here is a pure function of its inputs, which
is why §11.1 targets it for ≥90 % host coverage.

Depends on: `core` only.
