// CASE 2 -- pressing the knob steps the mode, and each mode lights its own status pixel.
//
// This is the shortest complete loop in the whole instrument: a click on the page becomes
// `sim press`, the fake closes ENC_SW, the ui HSM advances, it paints a pixel, and the pixel
// comes back in the next state frame.  Nothing in between is faked by this suite.
//
// What each pattern MEANS is case 12's job; this one only cares that the right pixel, and
// only that pixel, is doing something.
//
// Chain order (FIRMWARE.md §9.2): 0-1 dial, 2 bell, 3 alarm, 4 clock, 5 vol, 6 batt.
'use strict';

const { test, expect } = require('./harness');

const kBell = 2, kAlarm = 3, kClock = 4, kVol = 5;

// Exactly one pixel is doing anything, and it is this one.  Every cue animates, so the row
// has to be WATCHED rather than read -- a breathing pixel is legitimately dark at the
// instant a single read would catch it -- and watched all at once, or the window for pixel 6
// is a different second of the animation from the window for pixel 2.
const kAll = [0, 1, 2, 3, 4, 5, 6];
const litSet = (w) => w.per.map((p, i) => (p.everLit ? i : -1)).filter((i) => i >= 0);

async function onlyLit(ux, which, ms = 1800) {
    const want = which === null ? [] : [which];
    // Exclusivity ARRIVES; it is not instant.  The pixel the last mode owned fades out over
    // a quarter of a second, so for that quarter second two pixels are legitimately lit --
    // and a short window on a breathing pixel can equally show none.  Poll until the row
    // settles, then confirm over a window long enough to contain a whole breath.
    await expect.poll(async () => litSet(await ux.watchMany(kAll, 400)),
                      { timeout: 6000, message: `waiting for only pixel ${which}` })
        .toEqual(want);
    expect(litSet(await ux.watchMany(kAll, ms)), 'a second pixel joined in').toEqual(want);
}

test('an ordinary click on press & hold steps idle -> bell and lights the bell', async ({ ux }) => {
    await expect(ux.page.locator('#pill-mode')).toHaveText('ui idle');
    await ux.page.locator('#press').click();
    await expect(ux.page.locator('#pill-mode')).toContainText('ui bell');

    // Disarmed shows the white die breathing; armed is red.  Case 12 pins the pattern.
    const bell = await ux.watch(kBell, 2200);
    expect(bell.everLit).toBe(true);
    expect(Math.abs(bell.peak.r - bell.peak.g)).toBeLessThan(30);
});

test('the press cycle walks the four status pixels and comes back to dark', async ({ ux }) => {
    // The modes are named after the icons on the plate, which is the only label a user sees.
    const cycle = [['bell', kBell], ['alarm', kAlarm], ['clock', kClock],
                   ['volume', kVol], ['idle', null]];
    for (const [mode, px] of cycle) {
        await ux.press(120);
        await expect(ux.page.locator('#pill-mode')).toContainText(`ui ${mode}`);
        if (px === null) {
            await ux.expectRowDark();
        } else {
            await onlyLit(ux, px);         // long enough to catch a whole breath
        }
    }
});

test('holding the knob past 800 ms is a long press: straight back to idle', async ({ ux }) => {
    await ux.press(120);
    await expect(ux.page.locator('#pill-mode')).toContainText('ui bell');
    await ux.press(1100);
    await expect(ux.page.locator('#pill-mode')).toHaveText('ui idle');
    await ux.expectRowDark();
});

test('five seconds without input drops back to idle on its own', async ({ ux }) => {
    await ux.press(120);
    await expect(ux.page.locator('#pill-mode')).toContainText('ui bell');
    // The pill counts down; the firmware's own timer is what expires.
    await expect(ux.page.locator('#pill-mode')).toHaveText('ui idle', { timeout: 9000 });
    await ux.expectRowDark();
});

test('a turn while idle is ignored by design -- and does not light anything', async ({ ux }) => {
    await ux.turn(6);
    // The knob count moved, so the gesture certainly arrived...
    await expect(ux.page.locator('#k-count')).not.toHaveText('0');
    // ...and the HSM still did nothing with it (FIRMWARE.md §6.6).
    await expect(ux.page.locator('#pill-mode')).toHaveText('ui idle');
    await ux.expectRowDark();
});

test('rotating in the bell mode arms the alarm, and the pixel turns red', async ({ ux }) => {
    await ux.press(120);
    await expect(ux.page.locator('#pill-mode')).toContainText('ui bell');
    await ux.turn(2);
    await expect(ux.page.locator('#c-alarm')).toContainText('armed');
    const bell = await ux.watch(kBell, 1200);
    expect(bell.peak.r).toBeGreaterThan(40);
    expect(bell.peak.g).toBe(0);
    expect(bell.peak.b).toBe(0);

    await ux.turn(-2);
    await expect(ux.page.locator('#c-alarm')).toContainText('off');
});
