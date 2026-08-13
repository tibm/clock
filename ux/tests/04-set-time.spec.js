// CASE 4 -- a time typed on the page ends up on the dial.
//
// The page never computes a hand position: it sends `chrono time set`, chrono works out the
// microsteps, motion drives there, and the angle comes back in a state frame.  So the check
// is: does the ANGLE agree with the TIME, both read off the page?
'use strict';

const { test, expect, angleDiff, kHomeOffsetDeg } = require('./harness');

test.beforeEach(async ({ ux }) => {
    await ux.home();
});

test('each preset time puts the hands where that time is', async ({ ux }) => {
    for (const t of ['07:38', '02:10', '12:30', '09:45', '11:55']) {
        await ux.page.locator(`button[data-cmd="chrono time set ${t}"]`).click();
        await expect(ux.page.locator('#pill-clock')).toHaveText(new RegExp(`^${t}:\\d\\d$`));
        await ux.expectDialShowsTime();
    }
});

test('12:30 is not 00:30 -- the hour hand wraps at twelve, the dial does not', async ({ ux }) => {
    await ux.page.locator('button[data-cmd="chrono time set 12:30"]').click();
    await ux.expectDialShowsTime();
    // 12:30 puts the hour hand at 15 deg past twelve, NOT at 195.
    expect(angleDiff((await ux.hands()).h, 15)).toBeLessThan(kHomeOffsetDeg + 1);
});

test('the clock keeps running, and the minute hand keeps up with it', async ({ ux }) => {
    await ux.page.locator('button[data-cmd="chrono time set 09:45"]').click();
    await ux.expectDialShowsTime();
    const before = (await ux.hands()).m;
    // 60 jumps a minute, so the minute hand should visibly creep: 0.1 deg per second.
    await ux.cli('sim warp 60');
    await ux.page.waitForTimeout(2500);
    await ux.cli('sim warp 1');
    const after = (await ux.hands()).m;
    expect(angleDiff(after, before)).toBeGreaterThan(3);
    await ux.expectDialShowsTime(kHomeOffsetDeg + 2.5);
});

test('`now` sends this browser\'s clock, to the second', async ({ ux }) => {
    const wall = await ux.page.evaluate(() => {
        const d = new Date();
        return { h: d.getHours(), m: d.getMinutes() };
    });
    await ux.page.locator('#btn-now').click();
    const two = (n) => String(n).padStart(2, '0');
    await expect(ux.page.locator('#pill-clock'))
        .toHaveText(new RegExp(`^${two(wall.h)}:${two(wall.m)}:\\d\\d$`));
    await ux.expectDialShowsTime();
});

test('a time typed into the CLI box goes the same way as a button', async ({ ux }) => {
    await ux.cli('chrono time set 04:20:30');
    await expect(ux.page.locator('#pill-clock')).toHaveText(/^04:20:3\d$/);
    await ux.expectDialShowsTime();
});
