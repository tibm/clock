// CASE 9 -- the two sliders that change how time reaches the hands.
//
// `warp` scales sim time against wall time; `jumps per minute` is how finely the running
// clock is rendered onto the hands.  They sound alike and are not, so both are pinned here.
'use strict';

const { test, expect, angleDiff } = require('./harness');

const simSeconds = async (ux) => parseFloat((await ux.text('pill-sim')).replace(/[^\d.]/g, ''));

test('the warp slider really speeds sim time up', async ({ ux }) => {
    await expect(ux.page.locator('#pill-warp')).toHaveText('warp 1.00×');
    const t0 = await simSeconds(ux);
    await ux.page.waitForTimeout(1000);
    const t1 = await simSeconds(ux);
    expect(t1 - t0).toBeGreaterThan(0.6);
    expect(t1 - t0).toBeLessThan(1.6);

    // The slider is logarithmic: 0.1x to 1000x, with the interesting settings decades apart.
    // Notch 69 is 10^((69-25)/25) = 57.54x -- the scale is continuous, so a round number on
    // the dial is a coincidence and not something to assert.
    await ux.slide('r-warp', 69);
    await expect(ux.page.locator('#pill-warp')).toHaveText('warp 57.54×');
    const t2 = await simSeconds(ux);
    await ux.page.waitForTimeout(1000);
    const t3 = await simSeconds(ux);
    expect(t3 - t2).toBeGreaterThan(30);

    await ux.slide('r-warp', 25);
    await expect(ux.page.locator('#pill-warp')).toHaveText('warp 1.00×');
});

test('jumps per minute is how OFTEN, not how fast: 1 makes the hands tick', async ({ ux }) => {
    await ux.home();
    await ux.page.locator('button[data-cmd="chrono time set 09:45"]').click();
    await expect(ux.page.locator('#pill-motion')).toHaveText('motion idle');

    await ux.slide('r-steps', 1);
    await expect(ux.page.locator('#v-steps')).toHaveText('1');
    await expect(ux.page.locator('#steps-hint')).toContainText('one jump a minute');
    // The firmware took it, and says so in its own words.
    await ux.cli('chrono steps');
    await expect(ux.page.locator('#log')).toContainText('steps_per_minute 1');

    // A minute of clock time at 60x is a second of ours.  With one position per minute the
    // minute hand must land on a whole 6-degree multiple every time we look.
    await ux.slide('r-warp', 69);
    for (let i = 0; i < 4; i++) {
        await ux.page.waitForTimeout(400);
        const m = (await ux.hands()).m;
        const nearest = Math.round(m / 6) * 6;
        expect(angleDiff(m, nearest),
               `minute hand at ${m.toFixed(2)} deg is between two ticks`).toBeLessThan(3.5);
    }
    await ux.slide('r-warp', 25);
});

test('the hint under the slider matches what was sent', async ({ ux }) => {
    await ux.slide('r-steps', 12);
    await expect(ux.page.locator('#steps-hint')).toContainText('one move every 5.0 s');
    await ux.cli('chrono steps');
    await expect(ux.page.locator('#log')).toContainText('steps_per_minute 12');
});

test('the mechanism sliders reach motion tune', async ({ ux }) => {
    await ux.slide('r-vmax', 12000);
    await ux.slide('r-vcoarse', 2500);
    await ux.slide('r-backlash', 120);
    await ux.cli('motion tune');
    // Poll: the reply is a `res` frame that has to come back, and a plain snapshot of the
    // log races it.
    await expect.poll(() => ux.logText(), { timeout: 10000 })
        .toMatch(/v_max=12000 accel=\d+ v_coarse=2500 v_fine=\d+ backlash=120/);
});
