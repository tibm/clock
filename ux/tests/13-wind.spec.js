// CASE 13 -- winding a time, and the minute hand that used to run backwards.
//
// Setting a time is the one gesture where the hands are not following a clock but following
// YOU, and the two are not the same rule.  A clock takes the shortest way round, because the
// value it tracks moves a minute at a time.  A knob cannot: wind past the half hour and the
// minute hand's next position is more than half a turn ahead, so "shortest" is *backwards*.
// The hour hand moves twelve times slower and never comes near that limit, which is why this
// read as "the minute hand is flaky" rather than as one wrong line (FIRMWARE.md §16).
//
// So: two full turns of the hour hand, in forty-minute steps, watching the minute hand every
// ten milliseconds of the way.  Forty because it is not a factor of sixty -- a step of thirty
// or sixty moves the minute hand half a turn or none at all and proves nothing about which
// way it went.
'use strict';

const { test, expect, kRev } = require('./harness');

const kStep = 40;                          // minutes per step
const kSteps = 36;                         // ... and 36 x 40 min = 24 h = two turns of the hour
const kPerMinuteHand = kStep * kRev / 60;  // 11 520 usteps = 240 degrees
const kPerHourHand = kStep * kRev / 720;   // 960 usteps = 20 degrees

// One count is one minute, at any speed.  The acceleration curve is case 12's subject and is
// noise here: this case is about DIRECTION, and it needs each `sim knob` to be a known number
// of minutes rather than a number that depends on how loaded the machine is.
async function armTheKnob(ux) {
    await ux.cli('ui knob counts 1');
    await ux.cli('ui knob slow 100000');   // never past slow_max, so the gain is always 1
    // 24 revolutions of dial is 70 s of hand at the shipping speed, and this suite is not the
    // place to sit through it.  The fake integrates position exactly at any velocity, and the
    // page has sliders for both of these -- it is a bench setting, not a back door.
    await ux.cli('motion tune v_max 48000');
    await ux.cli('motion tune accel 400000');
}

// One wind, all the way round twice.  Every step is polled to its EXACT landing place, so no
// two steps are ever in flight together -- overlapping them would let the firmware legally
// drop a whole revolution and the totals below would stop meaning anything.
async function windADay(ux, dir) {
    const start = await ux.resting();
    await ux.startHandWatch();

    for (let k = 1; k <= kSteps; k++) {
        await ux.cli(`sim knob ${kStep * dir}`);
        const wantM = start.rawM + dir * k * kPerMinuteHand;
        const wantH = start.rawH + dir * k * kPerHourHand;
        await expect.poll(async () => {
            const u = await ux.usteps();
            return `${u.rawH}/${u.rawM}`;
        }, { timeout: 20000, message: `step ${k} of ${kSteps} never landed` })
            .toBe(`${wantH}/${wantM}`);
    }
    return await ux.stopHandWatch();
}

test('winding forward: two turns of the hour hand, and the minute hand never goes back',
    async ({ ux }) => {
    test.slow();
    await ux.home();
    await armTheKnob(ux);
    await ux.toMode('alarm');

    const seen = await windADay(ux, +1);

    // Not one sample backwards, on either hand.  This is the assertion the bug fails: under
    // the old rule every one of these steps took the minute hand 120 degrees ANTICLOCKWISE.
    expect(seen.m.min, 'the minute hand moved backwards under a clockwise wind').toBe(0);
    expect(seen.h.min, 'the hour hand moved backwards under a clockwise wind').toBe(0);
    expect(seen.samples).toBeGreaterThan(200);

    // And it did the whole journey: 24 h of winding is two turns of the hour hand and
    // twenty-four of the minute hand, every one of them actually travelled.
    expect(seen.h.net).toBe(2 * kRev);
    expect(seen.m.net).toBe(24 * kRev);
});

test('winding back: the same, anticlockwise', async ({ ux }) => {
    test.slow();
    await ux.home();
    await armTheKnob(ux);
    await ux.toMode('alarm');

    const seen = await windADay(ux, -1);

    expect(seen.m.max, 'the minute hand moved forwards under an anticlockwise wind').toBe(0);
    expect(seen.h.max, 'the hour hand moved forwards under an anticlockwise wind').toBe(0);
    expect(seen.h.net).toBe(-2 * kRev);
    expect(seen.m.net).toBe(-24 * kRev);
});

// The same bug at its smallest, in the other mode that has it: thirty-one minutes forward is
// 186 degrees of minute hand -- and the shortest way to 186 degrees is 174 degrees back.
test('clock: thirty-one minutes forward moves the minute hand forward', async ({ ux }) => {
    await ux.home();
    await ux.cli('ui knob counts 1');
    await ux.cli('ui knob slow 100000');
    await ux.page.locator('button[data-cmd="chrono time set 12:30"]').click();
    await expect(ux.page.locator('#pill-clock')).toHaveText(/^12:30/);

    await ux.toMode('clock');
    const start = await ux.resting();

    await ux.startHandWatch();
    await ux.cli('sim knob 31');
    await expect.poll(async () => (await ux.usteps()).rawM,
                      { timeout: 20000, message: 'the minute hand never reached 12:31' })
        .toBe(start.rawM + 31 * kRev / 60);
    const forth = await ux.stopHandWatch();
    expect(forth.m.min, 'the minute hand took the short way, which is backwards').toBe(0);
    expect(forth.m.net).toBe(31 * kRev / 60);

    // ... and thirty-one minutes back is a move that was never broken -- under half a turn,
    // where shortest and directed agree.  It still has to go the way the knob went.
    await ux.startHandWatch();
    await ux.cli('sim knob -31');
    await expect.poll(async () => (await ux.usteps()).rawM, { timeout: 20000 })
        .toBe(start.rawM);
    const back = await ux.stopHandWatch();
    expect(back.m.max).toBe(0);
    expect(back.m.net).toBe(-31 * kRev / 60);
});
