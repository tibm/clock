// CASE 10 -- the two ways of starting over, which are deliberately different.
//
// `sim reset` puts the fake HARDWARE back to power-on and leaves the services believing
// exactly what they believed -- motion still thinks it is homed while the hands have jumped.
// `sys reboot` restarts the image, so nothing carries over at all.
'use strict';

const { test, expect, angleDiff } = require('./harness');

test('reset fakes moves the hands and does NOT tell the firmware', async ({ ux }) => {
    await ux.home();
    await ux.page.locator('button[data-cmd="chrono time set 07:38"]').click();
    await ux.expectDialShowsTime();
    const before = await ux.hands();

    await ux.page.locator('button[data-cmd="sim reset"]').click();
    await expect.poll(async () => angleDiff((await ux.hands()).h, before.h),
                      { timeout: 8000 }).toBeGreaterThan(5);

    // The gap is the point: motion goes on believing it is homed while the dial has jumped,
    // so the clock on screen is now a lie and nothing in the firmware knows it.
    await expect(ux.page.locator('#btn-home')).toHaveClass(/\bok\b/);
    expect(await ux.dialTimeError()).toBeGreaterThan(5);

    // And homing is what closes it: a fresh zero, then straight back onto the time.
    await ux.home();
    await ux.expectDialShowsTime();
});

test('reboot restarts the image, and the page picks it back up', async ({ ux }) => {
    await ux.home();
    await ux.page.locator('button[data-cmd="chrono time set 07:38"]').click();
    await expect(ux.page.locator('#btn-home')).toHaveClass(/\bok\b/);

    await ux.page.locator('button[data-cmd="sys reboot"]').click();

    // The socket drops and app.js reconnects on its own.
    await expect(ux.page.locator('#conn')).toContainText('clock-sim', { timeout: 20000 });
    await expect(ux.page.locator('#conn')).toHaveClass(/\bup\b/, { timeout: 20000 });

    // Nothing survived: not the zero, not the time.
    await expect(ux.page.locator('#pill-motion')).toHaveText('motion uninit');
    await expect(ux.page.locator('#btn-home')).toHaveClass(/\bbad\b/);
    await expect(ux.page.locator('#pill-clock')).toHaveText('--:--:--');
    // And it is a working image, not a corpse.
    await ux.home();
});

test('the page survives a reload without losing what the firmware knows', async ({ ux }) => {
    await ux.home();
    await ux.page.locator('button[data-cmd="chrono time set 11:55"]').click();
    await expect(ux.page.locator('#pill-clock')).toHaveText(/^11:55/);

    await ux.page.reload();
    await expect(ux.page.locator('#swatches i')).toHaveCount(7);
    await expect(ux.page.locator('#pill-clock')).toHaveText(/^11:5\d/);
    await expect(ux.page.locator('#btn-home')).toHaveClass(/\bok\b/);
});
