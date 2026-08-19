// CASE 15 -- the dial finds up (§6.1d).
//
// Stand the cube on a different face and the movement is still perfectly homed while the
// dial reads three hours slow: the index, the opto and both shafts turned with it.  So the
// firmware turns the 12 to meet gravity, in thirty-degree steps, and the check here is the
// one a person would make -- turn the clock, and the time is still upright on the screen.
'use strict';

const { test, expect, angleDiff } = require('./harness');

test.beforeEach(async ({ ux }) => {
    await ux.home();
    await ux.page.locator('button[data-cmd="chrono time set 07:38"]').click();
    await ux.expectDialShowsTime();
});

test('a cube on its side still reads the time, and nothing moved on screen', async ({ ux }) => {
    const before = await ux.hands();
    await expect(ux.page.locator('#m-dial')).toHaveText('printed');

    // Ninety degrees clockwise: the printed 12 now points along the shelf and the dot at the
    // top of the plate is the printed 9 -- nine ticks round the other way.
    await ux.slide('r-yaw', 90);
    await expect(ux.page.locator('#m-dial')).toHaveText('9/12 · +270°');
    await ux.expectDialShowsTime();

    // And the strong version of the same claim: on SCREEN the hands did not move at all.
    // The plate turned ninety degrees and the hands turned two hundred and seventy the other
    // way inside it, which is a full circle between them -- so the time sits where it did.
    const after = await ux.hands();
    expect(angleDiff(after.m + 90, before.m)).toBeLessThan(4);
    expect(angleDiff(after.h + 90, before.h)).toBeLessThan(4);

    // Stand it back up and the dial goes back to being printed.
    await ux.slide('r-yaw', 0);
    await expect(ux.page.locator('#m-dial')).toHaveText('printed');
    await ux.expectDialShowsTime();
});

test('every face is a whole tick, and it goes round in order', async ({ ux }) => {
    for (const [yaw, tick] of [[30, 11], [90, 9], [180, 6], [270, 3], [330, 1]]) {
        await ux.slide('r-yaw', yaw);
        await expect(ux.page.locator('#m-dial')).toHaveText(`${tick}/12 · +${tick * 30}°`);
        await ux.expectDialShowsTime();
    }
});

test('a lean is not a new orientation until it is well past halfway', async ({ ux }) => {
    // Nobody sets a cube down square.  Ten degrees of lean is the same shelf it was.
    await ux.slide('r-yaw', 10);
    await ux.page.waitForTimeout(1600);  // three polls at the plugged cadence
    await expect(ux.page.locator('#m-dial')).toHaveText('printed');

    // Twenty is past the halfway line between two dots and STILL holds: the six degrees of
    // hysteresis are what stop a cube sitting on the line from flipping every poll.
    await ux.slide('r-yaw', 20);
    await ux.page.waitForTimeout(1600);
    await expect(ux.page.locator('#m-dial')).toHaveText('printed');

    // Far enough past it, and the dial does move.
    await ux.slide('r-yaw', 26);
    await expect(ux.page.locator('#m-dial')).toHaveText('11/12 · +330°');
});

test('flat on its back, the printed 12 is the 12', async ({ ux }) => {
    await ux.slide('r-yaw', 90);
    await expect(ux.page.locator('#m-dial')).toHaveText('9/12 · +270°');

    // Tip it onto its back: gravity is now perpendicular to the glass and there is no `up` on
    // the dial to find.  The printed 12 wins, which is what a clock on its back has always
    // done -- and the hands go back to reading the time against it.
    await ux.cli('sim imu 90 85');
    await expect(ux.page.locator('#m-dial')).toHaveText('printed');
    await ux.expectDialShowsTime();

    // Stand it up again and it finds the room a second time.
    await ux.cli('sim imu 90 0');
    await expect(ux.page.locator('#m-dial')).toHaveText('9/12 · +270°');
    await ux.expectDialShowsTime();
});

test('`motion tune level 0` pins the dial to the printed 12', async ({ ux }) => {
    await ux.slide('r-yaw', 180);
    await expect(ux.page.locator('#m-dial')).toHaveText('6/12 · +180°');

    // Off is a request for the printed dial BACK, not for the last tick frozen in place --
    // gravity has not changed and will not ask again.
    await ux.cli('motion tune level 0');
    await expect(ux.page.locator('#m-dial')).toHaveText('printed');
    await ux.expectDialShowsTime();

    // ... and with it off, turning the cube does nothing at all.
    await ux.slide('r-yaw', 270);
    await ux.page.waitForTimeout(1600);
    await expect(ux.page.locator('#m-dial')).toHaveText('printed');

    await ux.cli('motion tune level 1');
    await expect(ux.page.locator('#m-dial')).toHaveText('3/12 · +90°');
});
