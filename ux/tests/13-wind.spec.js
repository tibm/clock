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
//
// The last three cases are the same question asked of a DRAGGED knob rather than a stepped
// one -- counts arriving faster than the hands can move, which is the case a step-by-step
// test never sees and the one a finger produces every time (§16c).
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
    // A wind takes a couple of minutes now that every step is delivered at the speed the hands
    // can draw it (§6.6d), and the five-second timeout would drop the mode somewhere in the
    // middle of it -- after which a turn is correctly ignored and the case fails as "step 36
    // never landed".  A bench setting, not a back door: the page has the same knob.
    await ux.cli('ui knob timeout 60000');
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
        // A TURN, not a lump.  One count is one minute here, and a minute of dial takes a pace
        // whatever the movement is set to, so forty counts inside one poll are thirty-nine
        // minutes the hands cannot draw -- and, since 2026-08-17, thirty-nine that are dropped
        // rather than banked (§6.6d).  `over` is how the fake says "a finger did this".
        await ux.cli(`sim knob ${kStep * dir} over ${kStep * 25}`);
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
    await ux.page.locator('button[data-cmd="chrono time set 12:30"]').click();
    await expect(ux.page.locator('#pill-clock')).toHaveText(/^12:30/);

    await ux.toMode('clock');
    const start = await ux.resting();

    await ux.startHandWatch();
    await ux.cli('sim knob 31 over 1600');   // a turn: 31 minutes at a minute a pace (§6.6d)
    await expect.poll(async () => (await ux.usteps()).rawM,
                      { timeout: 20000, message: 'the minute hand never reached 12:31' })
        .toBe(start.rawM + 31 * kRev / 60);
    const forth = await ux.stopHandWatch();
    expect(forth.m.min, 'the minute hand took the short way, which is backwards').toBe(0);
    expect(forth.m.net).toBe(31 * kRev / 60);

    // ... and thirty-one minutes back is a move that was never broken -- under half a turn,
    // where shortest and directed agree.  It still has to go the way the knob went.
    await ux.startHandWatch();
    await ux.cli('sim knob -31 over 1600');
    await expect.poll(async () => (await ux.usteps()).rawM, { timeout: 20000 })
        .toBe(start.rawM);
    const back = await ux.stopHandWatch();
    expect(back.m.max).toBe(0);
    expect(back.m.net).toBe(-31 * kRev / 60);
});

// ---- a knob that is DRAGGED, which is what a finger does --------------------------------
//
// The cases above wait for each step to land, so the hands are never behind.  A finger does
// not wait: app.js coalesces a drag into one `sim knob n` every 33 ms, and at any speed worth
// calling a spin those arrive far faster than a 6000 ustep/s movement can answer.  That is
// where both of the 2026-08-16 reports lived, and neither is visible in a stepped test.
async function dragFor(ux, counts, times, gapMs) {
    await ux.startHandWatch();
    for (let i = 0; i < times; i++) {
        await ux.cli(`sim knob ${counts}`);
        await ux.page.waitForTimeout(gapMs);
    }
    await ux.page.waitForTimeout(2000);   // and let the hands finish the minute in flight
    return await ux.stopHandWatch();
}

test('a dragged knob winds the minute hand one way only -- clockwise', async ({ ux }) => {
    test.slow();
    await ux.home();
    await ux.toMode('alarm');
    await ux.resting();

    // 8 counts every 40 ms is two minutes per 40 ms at the shipping sensitivity: 3000 minutes
    // an hour of dial asked for, against a movement that can draw about twenty a second.
    const seen = await dragFor(ux, 8, 40, 40);

    expect(seen.m.min, 'the minute hand stepped backwards during a clockwise drag').toBe(0);
    expect(seen.h.min, 'the hour hand stepped backwards during a clockwise drag').toBe(0);
    // It moved, and it moved a lot -- this is not "monotone because it never budged".  It is
    // also worth less than the counts asked for, and that is the point: what the hands could
    // not draw is dropped, so the dial says what you set (§6.6d).
    expect(seen.m.net).toBeGreaterThan(kRev / 4);
    // ... and no more than the hands can draw: paced to the movement, not jumped.
    expect(seen.m.net).toBeLessThan(3 * kRev);
});

test('a dragged knob winds the minute hand one way only -- anticlockwise', async ({ ux }) => {
    test.slow();
    await ux.home();
    await ux.toMode('alarm');
    await ux.resting();

    // The report: "the hour hand correctly moves counter-clockwise, but the minute hand moves
    // clockwise".  It did -- the setting outran the hand until its target wrapped, and a hand
    // in flight was then re-aimed at somewhere it had already gone past, so it backed up.
    const seen = await dragFor(ux, -8, 40, 40);

    expect(seen.m.max, 'the minute hand stepped forwards during an anticlockwise drag').toBe(0);
    expect(seen.h.max, 'the hour hand stepped forwards during an anticlockwise drag').toBe(0);
    expect(seen.m.net).toBeLessThan(-kRev / 4);
    expect(seen.m.net).toBeGreaterThan(-3 * kRev);
});

test('a turn is worth the minutes you turned it, and arrives at a speed you can watch',
    async ({ ux }) => {
    await ux.home();
    await ux.toMode('alarm');
    await expect(ux.page.locator('#c-alarm')).toContainText('07:00');

    // Ten minutes as a real turn, at the shipping four counts a minute.  It used to be
    // multiplied twelvefold into two hours of dial in a single 20 ms poll (§6.6d).
    await ux.cli('sim knob 40 over 800');
    expect(await ux.text('c-alarm'), 'the whole turn landed in one tick').toContain('07:0');
    await expect(ux.page.locator('#c-alarm')).toContainText('07:10', { timeout: 6000 });
    // ... and the hands are where that says, not somewhere an hour away.
    await expect.poll(async () => (await ux.hands()).m, { timeout: 20000 })
        .toBeGreaterThan(55);
});

// §6.6d, changed 2026-08-17 -- and the report that caused it: "when the dial is released the
// hands still move, the minute hand by roughly 180 degrees".  They were paying out a bank of
// up to two seconds of winding, which at twenty minutes of dial a second is most of a turn
// arriving somewhere nobody chose.  The surplus is dropped now, so what is left to travel when
// the knob stops is the minute already in flight.
test('the hands stop when the knob stops', async ({ ux }) => {
    test.slow();
    await ux.home();
    await ux.toMode('alarm');
    await ux.resting();

    // A finger at four times what the hands can draw, for a second and a half.
    for (let i = 0; i < 12; i++) {
        await ux.cli('sim knob 8');
        await ux.page.waitForTimeout(40);
    }
    await ux.page.waitForTimeout(150);
    const atStop = (await ux.usteps()).rawM;
    const shownAtStop = await ux.text('c-alarm');

    await ux.page.waitForTimeout(2500);
    const after = (await ux.usteps()).rawM;
    // One minute of dial is 288 microsteps.  Two of them is a tenth of a second of hand.
    expect(Math.abs(after - atStop),
           'the minute hand wound on after the knob stopped').toBeLessThan(2 * kRev / 60);
    // And the SETTING is finished too: what the dial says is what you set.
    expect(await ux.text('c-alarm'), 'the alarm carried on winding').toBe(shownAtStop);
});
