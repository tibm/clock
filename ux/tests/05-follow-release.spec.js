// CASE 5 -- `follow` and `release`: does the wall clock drive the hands, or not?
//
// The pair is one fact shown twice, and the colour has to come from the firmware rather than
// from whichever button was clicked last -- otherwise a `chrono follow off` typed in the
// terminal would leave the page lying about it.
'use strict';

const { test, expect, angleDiff } = require('./harness');

test.beforeEach(async ({ ux }) => {
    await ux.home();
    await ux.page.locator('button[data-cmd="chrono time set 07:38"]').click();
    await expect(ux.page.locator('#pill-clock')).toHaveText(/^07:38/);
});

test('follow is lit at boot and release takes it', async ({ ux }) => {
    await expect(ux.page.locator('#btn-follow')).toHaveClass(/\bok\b/);
    await expect(ux.page.locator('#btn-release')).not.toHaveClass(/\bbad\b/);

    await ux.page.locator('#btn-release').click();
    await expect(ux.page.locator('#btn-release')).toHaveClass(/\bbad\b/);
    await expect(ux.page.locator('#btn-follow')).not.toHaveClass(/\bok\b/);
    // The pill says so too, with the pause mark.
    await expect(ux.page.locator('#pill-clock')).toContainText('⏸');
});

test('released, the clock runs on and the hands do not', async ({ ux }) => {
    // Let the 07:38 slew finish before we freeze anything.
    await expect(ux.page.locator('#pill-motion')).toHaveText('motion idle');
    await ux.page.locator('#btn-release').click();
    await expect(ux.page.locator('#pill-clock')).toContainText('⏸');

    const before = await ux.hands();
    await ux.page.locator('button[data-cmd="chrono time set 02:10"]').click();
    await expect(ux.page.locator('#pill-clock')).toHaveText(/^02:10/);
    await ux.page.waitForTimeout(1500);

    const after = await ux.hands();
    expect(angleDiff(after.h, before.h)).toBeLessThan(0.5);
    expect(angleDiff(after.m, before.m)).toBeLessThan(0.5);

    // And follow puts them back on the time that ran on without them.
    await ux.page.locator('#btn-follow').click();
    await expect(ux.page.locator('#btn-follow')).toHaveClass(/\bok\b/);
    await expect.poll(async () => angleDiff((await ux.hands()).m, 60), { timeout: 15000 })
        .toBeLessThan(4);
});

test('the buttons report the firmware, not the last click', async ({ ux }) => {
    // Released from the CLI -- the same line the terminal would send, from somewhere the
    // buttons know nothing about.
    await ux.cli('chrono follow off');
    await expect(ux.page.locator('#btn-release')).toHaveClass(/\bbad\b/);
    await ux.cli('chrono follow on');
    await expect(ux.page.locator('#btn-follow')).toHaveClass(/\bok\b/);
});
