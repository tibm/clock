// CASE 11 -- the dial as an input: dragging a hand, and the homing sensor that finds it.
//
// Dragging a hand is reaching into the case.  It changes where the hand IS and not where the
// firmware thinks it is -- which is the one thing on this page that must NOT round-trip.
'use strict';

const { test, expect, angleDiff, degOf } = require('./harness');

test('dragging a hand moves it without telling the firmware', async ({ ux }) => {
    await ux.home();
    const before = await ux.usteps();

    await ux.dragHand('m', 200);
    const after = await ux.hands();
    expect(angleDiff(after.m, 200)).toBeLessThan(12);      // roughly where it was dropped

    // The firmware's own idea of where it put that hand is untouched.
    expect(await ux.usteps()).toMatchObject({ h: before.h, m: before.m });
    await expect(ux.page.locator('#btn-home')).toHaveClass(/\bok\b/);

    // ... and the error is exactly what homing then finds.
    await ux.home();
    expect(angleDiff((await ux.hands()).m, degOf(4320))).toBeLessThan(3);
});

test('the scramble buttons move a hand somewhere new every time', async ({ ux }) => {
    const seen = [];
    for (let i = 0; i < 3; i++) {
        await ux.page.locator('button[data-rand="h"]').click();
        await ux.page.waitForTimeout(250);
        seen.push((await ux.hands()).h);
    }
    expect(new Set(seen.map((d) => d.toFixed(1))).size).toBe(3);
});

test('the opto meter reads the sensor, and the window glows when a hand is on it', async ({ ux }) => {
    await ux.placeHand('m', 180);
    await ux.placeHand('h', 180);
    await expect(ux.page.locator('#opto-txt')).toContainText('opto 0.080');
    expect(await ux.page.locator('#plate [data-role="opto"]').getAttribute('opacity'))
        .toBe('0.000');

    await ux.placeHand('m', 0);
    await expect(ux.page.locator('#opto-txt')).toContainText('opto 1.000');
    await expect(ux.page.locator('#opto-bar')).toHaveAttribute('style', /width: 100/);
    expect(parseFloat(await ux.page.locator('#plate [data-role="opto"]').getAttribute('opacity')))
        .toBeGreaterThan(0.9);
});

test('a held opto reading stays held until `opto auto` releases it', async ({ ux }) => {
    await ux.placeHand('m', 180);
    await ux.placeHand('h', 180);
    await ux.cli('sim opto 0.7');
    await expect(ux.page.locator('#opto-txt')).toContainText('opto 0.700 held');
    // Moving a hand onto the index no longer changes it -- that is what "held" means.
    await ux.placeHand('m', 0);
    await expect(ux.page.locator('#opto-txt')).toContainText('opto 0.700 held');

    await ux.page.locator('button[data-cmd="sim opto auto"]').click();
    await expect(ux.page.locator('#opto-txt')).toContainText('opto 1.000 auto');
});

test('the plate turns with the IMU, and a drag still lands where you dropped it', async ({ ux }) => {
    await ux.slide('r-yaw', 90);
    await expect(ux.page.locator('#v-yaw')).toHaveText('90°');
    const t = await ux.page.locator('#plate-rot').getAttribute('transform');
    expect(t).toMatch(/rotate\(90/);

    // The drag angle is taken in the DIAL's frame, so a tilted cube must not shift it.
    await ux.dragHand('m', 45);
    expect(angleDiff((await ux.hands()).m, 45)).toBeLessThan(12);
});

test('a knob turn is reported back as PCNT counts', async ({ ux }) => {
    await expect(ux.page.locator('#k-count')).toHaveText('0');
    await ux.turn(3);
    await expect(ux.page.locator('#k-count')).toHaveText('12');   // 4 counts per detent
    await ux.turn(-3);
    await expect(ux.page.locator('#k-count')).toHaveText('0');
});

test('the switch readout follows the button while it is held', async ({ ux }) => {
    const box = await ux.page.locator('#press').boundingBox();
    await ux.page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await ux.page.mouse.down();
    await expect(ux.page.locator('#k-sw')).toHaveText('DOWN');
    await ux.page.mouse.up();
    await expect(ux.page.locator('#k-sw')).toHaveText('up');
});
