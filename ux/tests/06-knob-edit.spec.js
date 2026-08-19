// CASE 6 -- the knob as the only control on the real clock.
//
// Press to pick what you are setting, turn to set it, and the HANDS are the readout: in the
// set modes they leave the wall clock and show what the knob is editing.  All of that is the
// ui HSM in firmware; the page only forwards detents.
'use strict';

const { test, expect, angleDiff } = require('./harness');

const hourDeg = (h, m) => (((h % 12) * 3600 + m * 60) / 43200) * 360;
const minuteDeg = (h, m) => ((m * 60) / 3600) * 360;

test('alarm: one detent is one minute, and the hands preview it', async ({ ux }) => {
    await ux.home();
    await ux.toMode('alarm');
    await expect(ux.page.locator('#c-alarm')).toContainText('07:00');

    await ux.turn(5);            // counts_per_minute is 4, and a detent is 4 counts
    await expect(ux.page.locator('#c-alarm')).toContainText('07:05');

    // The hands ARE the readout while setting (README §5).  Both of them, and they do not
    // arrive together -- the hour hand has much further to go from a fresh home.
    await expect.poll(async () => {
        const hands = await ux.hands();
        return Math.max(angleDiff(hands.m, minuteDeg(7, 5)), angleDiff(hands.h, hourDeg(7, 5)));
    }, { timeout: 20000 }).toBeLessThan(4);

    await ux.turn(-8);
    await expect(ux.page.locator('#c-alarm')).toContainText('06:57');
});

test('the alarm survives the drop back to idle', async ({ ux }) => {
    await ux.toMode('alarm');
    await ux.turn(10);
    await expect(ux.page.locator('#c-alarm')).toContainText('07:10');
    await expect(ux.page.locator('#pill-mode')).toHaveText('ui idle', { timeout: 9000 });
    await expect(ux.page.locator('#c-alarm')).toContainText('07:10');
});

test('clock previews on the hands and only commits on a long press', async ({ ux }) => {
    await ux.home();
    await ux.page.locator('button[data-cmd="chrono time set 02:10"]').click();
    await expect(ux.page.locator('#pill-clock')).toHaveText(/^02:10/);
    await expect(ux.page.locator('#pill-motion')).toHaveText('motion idle');

    await ux.toMode('clock');
    // Setting the clock releases the hands from it -- that is what makes a preview possible.
    await expect(ux.page.locator('#pill-clock')).toContainText('⏸');

    await ux.turn(20);
    // The hands moved to 02:30 ...
    await expect.poll(async () => angleDiff((await ux.hands()).m, minuteDeg(2, 30)),
                      { timeout: 15000 }).toBeLessThan(4);
    // ... while the clock itself has NOT been set yet.
    expect(await ux.text('pill-clock')).toMatch(/^02:1\d/);

    await ux.press(1000);                       // long press commits and drops to idle
    await expect(ux.page.locator('#pill-mode')).toHaveText('ui idle');
    await expect(ux.page.locator('#pill-clock')).toHaveText(/^02:30/);
    // ... and the hands are following the clock again.
    await expect(ux.page.locator('#pill-clock')).not.toContainText('⏸');
});

test('the sensitivity slider changes how far a detent goes', async ({ ux }) => {
    // 1 count per minute: a detent is 4 counts, so a detent is now four minutes.
    await ux.slide('r-cpm', 1);
    await expect(ux.page.locator('#v-cpm')).toHaveText('1');
    await ux.toMode('alarm');
    // Four minutes of dial is about a fifth of a second of minute hand, and the setting may
    // not outrun that (§6.6d) -- so a knob this sensitive has to be turned this slowly, and a
    // detent is then worth every one of its four minutes.
    await ux.turn(3, 280);
    await expect(ux.page.locator('#c-alarm')).toContainText('07:12');
});

test('the arrow keys are a detent too', async ({ ux }) => {
    await ux.toMode('alarm');
    await ux.arrowTurn(4);
    await expect(ux.page.locator('#c-alarm')).toContainText('07:04');
});

test('volume mode edits the volume, and the hands are the readout', async ({ ux }) => {
    await ux.home();
    await ux.toMode('volume');
    await expect(ux.page.locator('#c-vol')).toHaveText('40%');
    await ux.turn(20);
    await expect(ux.page.locator('#c-vol')).toHaveText('60%');
    // The pixel is a plain steady white now; the LEVEL is on the dial, where a percentage
    // can actually be read (case 12 pins the gauge).  60 % is 180 degrees round.
    await expect.poll(async () => (await ux.hands()).m, { timeout: 20000 })
        .toBeGreaterThan(170);
});
