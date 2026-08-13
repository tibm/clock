'use strict';

const { defineConfig } = require('@playwright/test');

module.exports = defineConfig({
    testDir: __dirname,
    // Every test spawns its own clocksim + uxapp.py on ports the kernel picked, so they are
    // independent -- but the whole point of this suite is a hand sweeping a dial in real
    // time, and eight browsers racing for the CPU is how a homing sweep starts missing the
    // index for reasons that have nothing to do with the firmware.
    workers: 2,
    fullyParallel: true,
    timeout: 60_000,
    expect: { timeout: 10_000 },
    reporter: [['list']],
    use: {
        // The Chrome that is installed, not a downloaded chromium: this page is a dev tool
        // for one laptop, and `npx playwright install` is a 150 MB answer to a question
        // nobody asked.
        channel: 'chrome',
        headless: true,
        viewport: { width: 1500, height: 1000 },
        actionTimeout: 10_000,
        trace: 'retain-on-failure',
        video: 'off',
    },
});
