// CASE 2 -- pressing the knob steps the mode, and each mode lights its own status pixel.
//
// This is the shortest complete loop in the whole instrument: a click on the page becomes
// `sim press`, the fake closes ENC_SW, the ui HSM advances, it paints a pixel, and the pixel
// comes back in the next state frame.  Nothing in between is faked by this suite.
//
// Chain order (FIRMWARE.md §9.2): 0-1 dial, 2 bell, 3 alarm, 4 clock, 5 vol, 6 batt.
'use strict';

const { test, expect } = require('./harness');

const kBell = 2, kAlarm = 3, kClock = 4, kVol = 5;

// Exactly one pixel is lit, and it is this one.
async function onlyLit(ux, which) {
    const px = await ux.pixels();
    const lit = px.map((p, i) => (p.lit ? i : -1)).filter((i) => i >= 0);
    expect(lit, `pixels lit: [${lit}]`).toEqual(which === null ? [] : [which]);
    return which === null ? null : px[which];
}

test('an ordinary click on press & hold steps idle -> alarm and lights the bell', async ({ ux }) => {
    await expect(ux.page.locator('#pill-mode')).toHaveText('ui idle');
    await ux.page.locator('#press').click();
    await expect(ux.page.locator('#pill-mode')).toContainText('ui alarm');
    const bell = await onlyLit(ux, kBell);
    // Disarmed shows the white die; armed is red.  60 % brightness, so w = 153.
    expect(bell.r).toBeGreaterThan(100);
    expect(bell.r).toEqual(Math.round(bell.r));   // not a red pixel: r == g == b-ish white
    expect(Math.abs(bell.r - bell.g)).toBeLessThan(20);
});

test('the press cycle walks the four status pixels and comes back to dark', async ({ ux }) => {
    const cycle = [['alarm', kBell], ['setalarm', kAlarm], ['setclock', kClock],
                   ['volume', kVol], ['idle', null]];
    for (const [mode, px] of cycle) {
        await ux.press(120);
        await expect(ux.page.locator('#pill-mode')).toContainText(`ui ${mode}`);
        await onlyLit(ux, px);
    }
});

test('holding the knob past 800 ms is a long press: straight back to idle', async ({ ux }) => {
    await ux.press(120);
    await expect(ux.page.locator('#pill-mode')).toContainText('ui alarm');
    await ux.press(1100);
    await expect(ux.page.locator('#pill-mode')).toHaveText('ui idle');
    await onlyLit(ux, null);
});

test('five seconds without input drops back to idle on its own', async ({ ux }) => {
    await ux.press(120);
    await expect(ux.page.locator('#pill-mode')).toContainText('ui alarm');
    // The pill counts down; the firmware's own timer is what expires.
    await expect(ux.page.locator('#pill-mode')).toHaveText('ui idle', { timeout: 9000 });
    await onlyLit(ux, null);
});

test('a turn while idle is ignored by design -- and does not light anything', async ({ ux }) => {
    await ux.turn(6);
    // The knob count moved, so the gesture certainly arrived...
    await expect(ux.page.locator('#k-count')).not.toHaveText('0');
    // ...and the HSM still did nothing with it (FIRMWARE.md §6.6).
    await expect(ux.page.locator('#pill-mode')).toHaveText('ui idle');
    await onlyLit(ux, null);
});

test('rotating in the bell mode arms the alarm, and the pixel turns red', async ({ ux }) => {
    await ux.press(120);
    await expect(ux.page.locator('#pill-mode')).toContainText('ui alarm');
    await ux.turn(2);
    await expect(ux.page.locator('#c-alarm')).toContainText('armed');
    const bell = await onlyLit(ux, kBell);
    expect(bell.r).toBeGreaterThan(100);
    expect(bell.g).toBe(0);
    expect(bell.b).toBe(0);

    await ux.turn(-2);
    await expect(ux.page.locator('#c-alarm')).toContainText('off');
});
