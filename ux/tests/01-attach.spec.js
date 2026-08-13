// CASE 1 -- the page attaches, and everything on it came from the firmware.
//
// Nothing is clicked here.  This is the baseline: if these fail, every other spec is
// measuring the wrong thing.
'use strict';

const { test, expect } = require('./harness');

test('the hello frame names the pixel chain, and the app does not invent it', async ({ ux }) => {
    const labels = await ux.page.locator('#swatches span').allTextContents();
    // px_names[] is sent by uibridge.cpp.  The order is the CHAIN order -- dial first, the
    // five status pixels through J12 after it -- and README §9 had it backwards until
    // 2026-08-09, so this is worth pinning.
    expect(labels.map((s) => s.trim())).toEqual([
        '0 dial0', '1 dial1', '2 bell', '3 alarm', '4 clock', '5 vol', '6 batt',
    ]);
    await expect(ux.page.locator('#conn')).toContainText('clock-sim');
    await expect(ux.page.locator('#conn')).toContainText('host');
});

test('a fresh boot is dark, unhomed and has no time', async ({ ux }) => {
    // "Zero emission when idle is a hard invariant" (FIRMWARE.md §6.6).
    for (const p of await ux.pixels()) expect(p.lit).toBe(false);

    // motion has never homed: the pill says so and the home button is red.
    await expect(ux.page.locator('#pill-motion')).toHaveText('motion uninit');
    expect(await ux.buttonState('btn-home')).toBe('bad');
    expect(await ux.text('m-faults')).toBe('0');
    expect(await ux.text('m-home')).toBe('—');

    // There is no RTC here, so chrono starts invalid and the pill must not pretend.
    expect(await ux.text('pill-clock')).toBe('--:--:--');
    expect(await ux.text('pill-mode')).toBe('ui idle');
});

test('the state frame keeps arriving -- sim time advances on its own', async ({ ux }) => {
    const first = parseFloat((await ux.text('pill-sim')).replace(/[^\d.]/g, ''));
    await ux.page.waitForTimeout(1200);
    const later = parseFloat((await ux.text('pill-sim')).replace(/[^\d.]/g, ''));
    expect(later).toBeGreaterThan(first + 0.8);
    expect(await ux.text('pill-warp')).toBe('warp 1.00×');
});
