// CASE 7 -- the two hardware switches and what hangs off them.
//
// The 12 V boost that feeds the wake light is gated on PD_PG (§6.8 interlock 4), so
// "unplugged" is not cosmetic: it takes a capability away, and the firmware has to say no.
'use strict';

const { test, expect } = require('./harness');

const kBatt = 6;

test('the plug toggle flips PD_PG and says which way round it is', async ({ ux }) => {
    await expect(ux.page.locator('#t-plug')).toContainText('plugged');
    await ux.page.locator('#t-plug').click();
    await expect(ux.page.locator('#t-plug')).toContainText('battery');
    await ux.page.locator('#t-plug').click();
    await expect(ux.page.locator('#t-plug')).toContainText('plugged');
});

test('two fast clicks are two flips, not one', async ({ ux }) => {
    // A toggle that decided what to send by reading the class it is currently painted with
    // lost this race: both clicks read the same stale frame and sent the same command.
    await ux.page.locator('#t-plug').click();
    await ux.page.locator('#t-plug').click();
    await expect(ux.page.locator('#t-plug')).toContainText('plugged');
    await expect(ux.page.locator('#t-plug')).toHaveClass(/\bon\b/);
});

test('the wake light is plugged-only, and the firmware is the one that refuses', async ({ ux }) => {
    await ux.cli('ui wake 40 10');
    await expect(ux.page.locator('#s-wake')).toHaveText('40 / 10 %');

    await ux.page.locator('#t-plug').click();
    await expect(ux.page.locator('#t-plug')).toContainText('battery');
    await ux.cli('ui wake 60 20');
    await expect(ux.page.locator('#log')).toContainText('plugged-only');
    // ... and the duty did not change behind the refusal.
    await expect(ux.page.locator('#s-wake')).not.toHaveText('60 / 20 %');
});

test('a flat cell on battery lights the batt pixel', async ({ ux }) => {
    await ux.page.locator('#t-plug').click();
    await expect(ux.page.locator('#t-plug')).toContainText('battery');
    // soc = (mV - 3300) / 7.5, so 3400 mV is 13 % -- under the 20 % warning.
    await ux.slide('r-vbat', 3400);
    await expect(ux.page.locator('#t-plug')).toContainText('13%');

    // It breathes rather than sitting on -- the one emitter that is allowed to be lit while
    // the clock is idle should be the least alarming thing on the plate.  Watching it is not
    // optional: a single read lands in the dark half of the breath often enough to flake.
    await expect.poll(async () => (await ux.watch(kBatt, 900)).everLit,
                      { timeout: 12000, message: 'the batt pixel never came on' }).toBe(true);
    const w = await ux.watch(kBatt, 3600);
    expect(w.peak.r).toBeGreaterThan(w.peak.b);       // amber, not white
    expect(w.levels, 'the warning should breathe, not sit on').toBeGreaterThan(4);
    expect(w.everDark).toBe(true);

    // Plugged back in, the warning goes away.
    await ux.page.locator('#t-plug').click();
    await expect.poll(async () => (await ux.watch(kBatt, 900)).everLit, { timeout: 12000 })
        .toBe(false);
});

test('a healthy cell lights nothing at all', async ({ ux }) => {
    await ux.page.locator('#t-plug').click();
    await ux.slide('r-vbat', 3900);
    await ux.page.waitForTimeout(1500);
    await ux.expectRowDark();
});
