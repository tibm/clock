// CASE 14 -- the clock finds its own zero, and remembers where it is.
//
// Three things that only make sense together (FIRMWARE.md §6.1):
//
//   * a booted clock HOMES, unasked.  The hands are wherever the last power-off left them and
//     nothing else can find that out, so every reading before it does is a guess.
//   * the index is not north.  The opto answers "the mark is over the window", and how far
//     that is from twelve o'clock is a fact about how one clock was assembled -- so there is a
//     trim per hand, set by eye on the calibration card, kept in storage.
//   * and the hands cross that same index every hour anyway, so the movement keeps itself
//     honest for free: a crossing that lands early or late trims the hand that made it.
'use strict';

const { test, expect, kRev, angleDiff } = require('./harness');

// 48 microsteps to the degree.  The one number this file knows, and the page prints it.
const kUstepsPerDeg = kRev / 360;

test.describe('boot homing', () => {
    // The product's own clocksim, with no --no-home: this is what the clock does when the USB
    // cable goes in.  Every other case in the suite runs with it off, because nine seconds of
    // sweeping before each of forty tests buys nothing that this one case does not prove.
    test.use({ simArgs: [] });

    test('a clock that has just booted homes without being asked', async ({ ux }) => {
        // The page attaches within a few hundred milliseconds of the process starting, so the
        // run is either already live or about to be -- and it finishes on its own.
        await expect(ux.page.locator('#btn-home')).toHaveClass(/\b(go|ok)\b/, { timeout: 20000 });
        await expect(ux.page.locator('#btn-home')).toHaveClass(/\bok\b/, { timeout: 45000 });
        await expect(ux.page.locator('#m-faults')).toHaveText('0');
        await expect(ux.page.locator('#m-home')).not.toHaveText('—');

        // And the hands really are where the firmware now says: `motion uninit` is gone, the
        // dial is trustworthy, and nobody clicked anything.
        await expect(ux.page.locator('#pill-motion')).not.toContainText('uninit');
    });
});

test('the calibration sliders move the hand, one per hand', async ({ ux }) => {
    await ux.home();
    // Both hands to twelve o'clock, which is where the trim is visible: the firmware says
    // 12:00 and the hand is a degree or two short of north, because the rising edge of the
    // index sits a mark's width before its centre.
    await ux.cli('motion goto 12:00');
    await ux.resting();
    const before = await ux.hands();
    expect(await ux.zeros()).toEqual({ h: 0, m: 0 });

    // Two degrees clockwise on the hour hand alone.
    await ux.slide('r-zeroh', 2 * kUstepsPerDeg);
    await expect(ux.page.locator('#v-zeroh')).toContainText('2.00°');
    await ux.resting();
    const after = await ux.hands();
    expect(angleDiff(after.h, before.h + 2), 'the hour hand did not move two degrees')
        .toBeLessThan(0.4);
    expect(angleDiff(after.m, before.m), 'the minute hand moved too').toBeLessThan(0.2);

    // And the other one, the other way.
    await ux.slide('r-zerom', -1 * kUstepsPerDeg);
    await ux.resting();
    const both = await ux.hands();
    expect(angleDiff(both.m, before.m - 1)).toBeLessThan(0.4);
    expect(await ux.zeros()).toEqual({ h: 2 * kUstepsPerDeg, m: -1 * kUstepsPerDeg });
});

test('the calibration survives a reboot, and the next home adopts it', async ({ ux }) => {
    await ux.home();
    await ux.slide('r-zeroh', 96);
    await ux.slide('r-zerom', -48);
    await expect.poll(() => ux.zeros(), { timeout: 5000 }).toEqual({ h: 96, m: -48 });

    // `sys reboot` re-execs the whole image: every service is constructed again and nothing
    // carries over except what was written down.  The page reconnects on its own.
    await ux.page.locator('button[data-cmd="sys reboot"]').click();
    await expect(ux.page.locator('#conn')).not.toHaveClass(/up/, { timeout: 10000 });
    await expect(ux.page.locator('#conn')).toHaveClass(/up/, { timeout: 20000 });
    await ux.page.waitForTimeout(600);

    expect(await ux.zeros(), 'the trim did not survive the reboot').toEqual({ h: 96, m: -48 });

    // ... and the hands land on it: home, then ask for twelve o'clock, and the hour hand sits
    // two degrees further round than the minute hand's own zero would put it.
    await ux.home();
    await ux.cli('motion goto 12:00');
    await ux.resting();
    const hands = await ux.hands();
    expect(angleDiff(hands.h, hands.m + 3), 'the two zeros are not 3 degrees apart')
        .toBeLessThan(0.6);
});

test('a hand that has drifted trims itself the next time it crosses the sensor',
    async ({ ux }) => {
    test.slow();
    await ux.home();
    // The hour hand well away from the index: both hands pass the same window, so a crossing
    // with the other hand sitting in it is unattributable on purpose (§6.1, rule 3).
    await ux.cli('motion goto 06:00');
    await ux.resting();

    // Slow enough that a crossing is a measurement: the opto is sampled once a control tick.
    await ux.cli('motion tune v_max 400');
    await ux.cli('sim warp 1');

    // Reach through the glass and nudge the minute hand half a degree.  The firmware is not
    // told -- that is the whole point of the gesture -- so its idea of the dial is now wrong.
    const drifted = ((await ux.hands()).m + 0.5) % 360;
    await ux.placeHand('m', drifted.toFixed(2));
    const before = await ux.trims();

    // Four degrees back into the dark, then forward across the index the way the clock does
    // every hour.  `motion step` is relative microsteps -- the bench command for exactly this.
    await ux.cli(`motion step m ${-4 * kUstepsPerDeg}`);
    await ux.resting();
    await ux.cli(`motion step m ${8 * kUstepsPerDeg}`);

    await expect.poll(() => ux.trims().then((t) => t.count), { timeout: 30000 })
        .toBeGreaterThan(before.count);
    const trim = await ux.trims();
    // The hand was AHEAD of where the firmware had it, so the edge arrived early and the
    // count had to catch up: the correction is positive, and it is the size of the drift.
    expect(trim.last).toBeGreaterThan(0);
    expect(trim.last).toBeLessThan(kUstepsPerDeg);   // under a degree -- it was half of one

    await ux.cli('motion tune v_max 6000');
});
