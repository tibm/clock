// CASE 3 -- `home` finds the index, wherever the hands were left.
//
// The whole point of the app: a hand you moved by hand is an error the firmware does not
// know about, and homing is what closes it.  The tests place the hands through the page (the
// same `sim hand` line the scramble buttons send) and then look only at what the dial and
// the MECHANISM card report afterwards.
'use strict';

const { test, expect, angleDiff, degOf, kHomeOffsetDeg } = require('./harness');

// Where a bare home leaves the mechanism: the minute hand is homed and then parked 90 deg
// away so it is off the sensor for the hour sweep; the hour hand finishes on its own index.
const kParked = 4320;   // kRev / 4

test('home from a scrambled dial ends with both hands on a known zero', async ({ ux }) => {
    await ux.placeHand('h', 137);
    await ux.placeHand('m', 41);
    // The firmware has not been told; the movement still says it has never homed.
    await expect(ux.page.locator('#pill-motion')).toHaveText('motion uninit');

    await ux.page.locator('#btn-home').click();
    // It reports being busy while it is busy -- and says which phase.
    await expect(ux.page.locator('#btn-home')).toHaveText('homing…');
    await expect(ux.page.locator('#btn-home')).toHaveClass(/\bgo\b/);
    await expect(ux.page.locator('#pill-motion')).toContainText('homing');

    await expect(ux.page.locator('#btn-home')).toHaveClass(/\bok\b/, { timeout: 45000 });
    await expect(ux.page.locator('#btn-home')).toHaveText('home');
    await expect(ux.page.locator('#m-faults')).toHaveText('0');
    await expect(ux.page.locator('#m-home')).not.toHaveText('—');

    const pos = await ux.usteps();
    expect(pos.h).toBe(0);
    expect(pos.m).toBe(kParked);

    // And the hands are physically there, give or take the systematic edge offset.
    const hands = await ux.hands();
    expect(angleDiff(hands.h, 0)).toBeLessThan(kHomeOffsetDeg);
    expect(angleDiff(hands.m, degOf(kParked))).toBeLessThan(kHomeOffsetDeg);
});

test('a hand parked ON the index still homes -- the sensor is cleared first', async ({ ux }) => {
    // The case the Clear phase exists for: the opto is lit at rest, so there is no rising
    // edge to find until something moves off the mark.
    await ux.placeHand('m', 0);
    await expect(ux.page.locator('#opto-txt')).toContainText(/opto 0\.9|opto 1\.0/);

    await ux.home();
    expect(await ux.text('m-faults')).toBe('0');
    const pos = await ux.usteps();
    expect(pos.h).toBe(0);
    expect(pos.m).toBe(kParked);
});

test('both hands on the index at once is still not a guess', async ({ ux }) => {
    await ux.placeHand('h', 0);
    await ux.placeHand('m', 0);
    await ux.home();
    expect(await ux.text('m-faults')).toBe('0');
});

test('homing twice in a row is idempotent', async ({ ux }) => {
    await ux.home();
    const first = await ux.usteps();
    await ux.placeHand('h', 200);
    await ux.home();
    expect(await ux.usteps()).toMatchObject({ h: first.h, m: first.m });
    expect(await ux.text('m-faults')).toBe('0');
});

test('a sweep that outruns the sensor faults, and the button says so in red', async ({ ux }) => {
    // Sampled homing can genuinely miss: the lit window is ~3.6 deg and the loop reads the
    // opto once a tick, so enough warp puts whole revolutions between two reads.  That
    // aliasing is modelled on purpose (hal_host.cpp) because the bench will show it.
    await ux.cli('sim warp 400');
    await ux.page.locator('#btn-home').click();

    await expect(ux.page.locator('#btn-home')).toHaveClass(/\bbad\b/, { timeout: 45000 });
    await expect(ux.page.locator('#pill-motion')).toContainText('fault');
    await expect(ux.page.locator('#m-faults')).not.toHaveText('0');

    // And it recovers: back to a sane speed, home again, clean.
    await ux.cli('sim warp 1');
    await ux.home();
    await expect(ux.page.locator('#pill-motion')).not.toContainText('fault');
});

test('stop aborts a run in progress and leaves it unhomed rather than lying', async ({ ux }) => {
    await ux.placeHand('h', 137);
    await ux.placeHand('m', 41);
    await ux.page.locator('#btn-home').click();
    await expect(ux.page.locator('#btn-home')).toHaveClass(/\bgo\b/);

    await ux.page.locator('button[data-cmd="motion stop"]').click();
    await expect(ux.page.locator('#pill-motion')).not.toContainText('homing');
    await expect(ux.page.locator('#btn-home')).toHaveClass(/\bbad\b/);
    // An abort is not a fault -- nothing failed, we asked it to stop.
    await expect(ux.page.locator('#m-faults')).toHaveText('0');
});
