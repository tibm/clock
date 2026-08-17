// CASE 12 -- the user experience, mode by mode (README §12, FIRMWARE.md §6.6).
//
// One press is one mode, each mode lights the pixel whose icon names it, and the pattern on
// that pixel is a sentence: a breath asks or answers the alarm question -- white for off, red
// for armed -- steady white means you are editing, three red flashes mean no.  The hands are
// the readout throughout, including in the two modes where what they show is not a time.
//
// Chain order (FIRMWARE.md §9.2): 0-1 dial, 2 bell, 3 alarm, 4 clock, 5 vol, 6 batt.
'use strict';

const { test, expect, angleDiff, kRev } = require('./harness');

const kBell = 2, kAlarm = 3, kClock = 4, kVol = 5, kBatt = 6;

const hourDeg = (h, m) => (((h % 12) * 3600 + m * 60) / 43200) * 360;
const minuteDeg = (h, m) => ((m * 60) / 3600) * 360;
// Both hands on the 6, which is what "the alarm is off" looks like: at 6:30 the hour hand is
// halfway to the 7, so a pair of hands agreeing on the 6 is a reading no clock can produce.
const kSouth = 180;

// Wait for the hands to arrive somewhere, in the dial's own degrees.
async function expectHands(ux, h, m, tol = 4) {
    await expect.poll(async () => {
        const at = await ux.hands();
        return Math.max(angleDiff(at.h, h), angleDiff(at.m, m));
    }, { timeout: 25000, message: `the hands never reached ${h.toFixed(1)}° / ${m.toFixed(1)}°` })
        .toBeLessThan(tol);
}

// ---- 1. bell: is the alarm on? ---------------------------------------------------------

test('bell, disarmed: the pixel breathes white and the hands stand on the 6', async ({ ux }) => {
    await ux.home();
    await ux.cli('chrono time set 03:20');
    await ux.toMode('bell');

    const bell = await ux.watch(kBell, 2200);
    expect(bell.everLit, 'the bell pixel never lit').toBe(true);
    expect(bell.peak.r, 'a disarmed bell must not be red').toBeLessThan(bell.peak.g + 30);
    // White on an RGBW pixel is the W die, which the page renders as near-neutral grey.
    expect(Math.abs(bell.peak.r - bell.peak.b)).toBeLessThan(60);
    // A breath is a curve that reaches zero: many distinct levels, and dark twice a cycle.
    expect(bell.levels, 'a breath should pass through many levels').toBeGreaterThan(4);
    expect(bell.everDark, 'a breath reaches zero').toBe(true);

    // ... and the hands say it too, with a reading no working clock can produce.
    await expectHands(ux, kSouth, kSouth);
});

test('bell: clockwise arms and the pixel breathes red, anticlockwise disarms', async ({ ux }) => {
    await ux.toMode('bell');
    await expect(ux.page.locator('#c-alarm')).toContainText('off');

    await ux.turn(1);                       // one detent clockwise is enough -- it is a direction
    await expect(ux.page.locator('#c-alarm')).toContainText('armed');

    const bell = await ux.watch(kBell, 2200);
    expect(bell.peak.r).toBeGreaterThan(40);
    expect(bell.peak.g).toBe(0);
    expect(bell.peak.b).toBe(0);
    // The armed cue BREATHES: same curve as the disarmed one, and the answer is the colour.
    // It used to be a hard-edged blink -- one brightness, on and off -- which reads as an
    // alarm going off rather than an alarm that is set (§6.6b, changed 2026-08-15).
    expect(bell.levels, 'armed is a breath, not a blink').toBeGreaterThan(4);
    expect(bell.everDark, 'a breath reaches zero').toBe(true);

    await ux.turn(-1);
    await expect(ux.page.locator('#c-alarm')).toContainText('off');
});

test('bell: armed, the hands show the alarm time; disarmed, back to the 6', async ({ ux }) => {
    await ux.home();
    await ux.toMode('bell');
    await ux.turn(1);
    await expect(ux.page.locator('#c-alarm')).toContainText('07:00 armed');
    await expectHands(ux, hourDeg(7, 0), minuteDeg(7, 0));

    await ux.turn(-1);
    await expect(ux.page.locator('#c-alarm')).toContainText('off');
    await expectHands(ux, kSouth, kSouth);
});

// ---- 2. alarm: what time? --------------------------------------------------------------

test('alarm: the pixel says the same thing the bell did', async ({ ux }) => {
    await ux.toMode('alarm');
    const disarmed = await ux.watch(kAlarm, 2200);
    expect(disarmed.everLit).toBe(true);
    expect(disarmed.levels, 'disarmed breathes').toBeGreaterThan(4);
    expect(disarmed.peak.r).toBeLessThan(disarmed.peak.g + 30);

    // Arm it from the bell mode and come back: the answer travels with the alarm, not the mode.
    await ux.toMode('bell');
    await ux.turn(1);
    await expect(ux.page.locator('#c-alarm')).toContainText('armed');
    await ux.toMode('alarm');
    const armed = await ux.watch(kAlarm, 2200);
    expect(armed.peak.r).toBeGreaterThan(40);
    expect(armed.peak.g).toBe(0);
    expect(armed.levels, 'armed breathes too -- only the colour differs').toBeGreaterThan(4);
    expect(armed.everDark).toBe(true);
});

test('alarm: a detent is a minute, and a spin is worth the minutes you spun', async ({ ux }) => {
    await ux.toMode('alarm');
    await expect(ux.page.locator('#c-alarm')).toContainText('07:00');

    // ux.turn() waits 45 ms between detents, which is two ui ticks -- as slow as a hand.
    await ux.turn(3);
    await expect(ux.page.locator('#c-alarm')).toContainText('07:03');

    // The same knob, spun.  40 counts is ten minutes at four counts a minute, and that is
    // exactly what it is worth -- they are BANKED and paid out at the speed the hands can
    // draw, not multiplied twelvefold into two hours of dial inside one 20 ms poll.  A
    // setting that outruns its own readout stops meaning anything (FIRMWARE.md §6.6d).
    await ux.cli('sim knob 40');
    await expect(ux.page.locator('#c-alarm')).toContainText('07:13', { timeout: 5000 });
    await ux.page.waitForTimeout(1000);
    await expect(ux.page.locator('#c-alarm'), 'the spin carried on winding after it was spent')
        .toContainText('07:13');
});

test('alarm: the hands track what is being set, hour hand included', async ({ ux }) => {
    await ux.home();
    await ux.toMode('alarm');
    await ux.turn(20);
    await expect(ux.page.locator('#c-alarm')).toContainText('07:20');
    await expectHands(ux, hourDeg(7, 20), minuteDeg(7, 20));
});

// ---- 3. clock: what time is it? --------------------------------------------------------

test('clock: steady white, and the hands preview the time being set', async ({ ux }) => {
    await ux.home();
    await ux.cli('chrono time set 02:10');
    await expect(ux.page.locator('#pill-clock')).toHaveText(/^02:10/);
    await ux.toMode('clock');

    // Steady, not breathing: it comes up on a fade and then holds.  Sample after the fade.
    await ux.page.waitForTimeout(400);
    const clock = await ux.watch(kClock, 1500);
    expect(clock.everDark, 'setting the clock is a steady light').toBe(false);
    expect(clock.levels, 'and a steady one holds ONE level').toBe(1);
    expect(clock.peak.r).toBeGreaterThan(40);
    expect(Math.abs(clock.peak.r - clock.peak.b)).toBeLessThan(60);   // white, not red

    await ux.turn(20);
    await expectHands(ux, hourDeg(2, 30), minuteDeg(2, 30));
    expect(await ux.text('pill-clock')).toMatch(/^02:1\d/);           // not committed yet
    await ux.press(1000);
    await expect(ux.page.locator('#pill-clock')).toHaveText(/^02:30/);
});

test('clock: opens on the time the clock is keeping', async ({ ux }) => {
    await ux.home();
    await ux.cli('chrono time set 09:45');
    await expect(ux.page.locator('#pill-clock')).toHaveText(/^09:4[56]/);
    await ux.toMode('clock');
    // Before any turn at all: the mode starts where the clock is, so the first detent is a
    // nudge to the real time rather than a jump from somewhere else.
    await expectHands(ux, hourDeg(9, 45), minuteDeg(9, 45), 6);
});

test('clock: with no time ever set, it opens at 12:00', async ({ ux }) => {
    // A fresh clocksim has never been told the time -- there is no RTC (FIRMWARE.md §6.4) --
    // and chrono's hour and minute are then an offset from an epoch it never had.  That reads
    // as minutes-since-boot, and the mode used to open with the hands pointing at the uptime.
    await ux.home();
    await expect(ux.page.locator('#pill-clock')).toHaveText('--:--:--');
    await ux.toMode('clock');
    await expectHands(ux, 0, 0);
});

test('clock: with the network holding the time, it flashes red and skips to volume',
    async ({ ux }) => {
    // Two facts make the network the time authority; the third is the rear toggle, which is
    // already on.  The page's own control sends them.
    await ux.page.locator('#t-net').click();
    await expect(ux.page.locator('#t-net')).toContainText('clock locked');

    await ux.toMode('alarm');
    // Watching BEFORE the press: the burst is ~600 ms and starts the moment the press lands.
    const burst = ux.watch(kClock, 2000);
    await ux.press(120);
    const flashes = await burst;

    await expect(ux.page.locator('#pill-mode')).toContainText('ui volume');
    expect(flashes.everLit, 'the refusal was silent').toBe(true);
    expect(flashes.peak.r, 'a refusal is red').toBeGreaterThan(40);
    expect(flashes.peak.g).toBe(0);
    expect(flashes.peak.b).toBe(0);
    expect(flashes.everDark, 'a burst ends').toBe(true);

    // And it really is a burst, not a blink: the clock pixel is dark again a second later
    // while volume carries on.
    const after = await ux.watch(kClock, 900);
    expect(after.everLit, 'the refusal never stopped flashing').toBe(false);
    expect((await ux.watch(kVol, 400)).everLit).toBe(true);
});

test('clock: the rear toggle gives the time back to the knob', async ({ ux }) => {
    await ux.page.locator('#t-net').click();
    await expect(ux.page.locator('#t-net')).toContainText('clock locked');
    await ux.page.locator('#t-radio').click();                 // radios off
    await expect(ux.page.locator('#t-net')).toContainText('radios off');

    await ux.toMode('clock');
    await expect(ux.page.locator('#pill-mode')).toContainText('ui clock');
});

// ---- 4. volume -------------------------------------------------------------------------

test('volume: the hands are a gauge, 12:00 = 0 % and 10:00 = 100 %', async ({ ux }) => {
    await ux.home();
    await ux.toMode('volume');
    await expect(ux.page.locator('#c-vol')).toHaveText('40%');

    // 300 degrees of dial for the whole range, both hands together.
    await expectHands(ux, 40 * 3, 40 * 3);

    await ux.cli('sim knob 40');                               // a spin: straight to the top
    await expect(ux.page.locator('#c-vol')).toHaveText('100%');
    await expectHands(ux, 300, 300);

    await ux.cli('sim knob -400');
    await expect(ux.page.locator('#c-vol')).toHaveText('0%');
    await expectHands(ux, 0, 0);
});

test('volume: the gauge is swept, never cut across the 10 and the 12', async ({ ux }) => {
    // The scale runs clockwise from the 12 to the 10 and the last 60 degrees are off it.  The
    // hands must SWEEP the scale, whichever way the level is going: the shortest way from
    // 30 % to 100 % is backwards over the 12, which is the wrong direction for a level going
    // up AND a trip through the one part of the dial the gauge does not use.
    await ux.home();
    await ux.cli('ui knob counts 1');
    await ux.cli('ui knob slow 100000');      // one count, one percent, at any speed
    await ux.toMode('volume');
    await ux.cli('sim knob -200');            // hard down, and let it get there
    await expect(ux.page.locator('#c-vol')).toHaveText('0%');
    await expectHands(ux, 0, 0);
    const zero = await ux.resting();

    await ux.startHandWatch();
    await ux.cli('sim knob 30');
    await expect(ux.page.locator('#c-vol')).toHaveText('30%');
    await expectHands(ux, 90, 90);
    await ux.cli('sim knob 70');
    await expect(ux.page.locator('#c-vol')).toHaveText('100%');
    await expectHands(ux, 300, 300);
    await ux.resting();                       // exactly there, not within four degrees of it
    const up = await ux.stopHandWatch();

    // Up is clockwise, the whole 300 degrees of scale -- not -60 across the dead zone.
    expect(up.m.net).toBe(kRev * 300 / 360);
    expect(up.m.min, 'the gauge went backwards on its way up').toBe(0);
    const dead = (a) => a > kRev * 300 / 360 && a < kRev;
    expect(up.angles.filter((a) => dead(a.m) || dead(a.h)).length,
           'a hand was between the 10 and the 12, which is off the scale').toBe(0);

    await ux.startHandWatch();
    await ux.cli('sim knob -100');
    await expect(ux.page.locator('#c-vol')).toHaveText('0%');
    await expectHands(ux, 0, 0);
    await ux.resting();
    const down = await ux.stopHandWatch();
    expect(down.m.net).toBe(-kRev * 300 / 360);   // ... and down is anticlockwise
    expect(down.m.max, 'the gauge went forwards on its way down').toBe(0);
    expect(down.angles.filter((a) => dead(a.m) || dead(a.h)).length).toBe(0);

    // Both hands, together, the whole way: a gauge with two needles that disagree is not one.
    expect((await ux.resting()).rawH - zero.rawH).toBe(0);
});

test('volume: solid white, and the level is played back while you set it', async ({ ux }) => {
    await ux.toMode('volume');
    await ux.page.waitForTimeout(400);
    const vol = await ux.watch(kVol, 1200);
    expect(vol.everDark, 'volume is a steady light').toBe(false);
    expect(vol.levels).toBe(1);

    // The chime: the speaker really is driven, at the level being edited.  It repeats, so
    // watching the readout for two seconds is enough to catch one.
    const heard = await ux.page.evaluate(() => new Promise((resolve) => {
        const el = document.querySelector('#s-spk');
        let on = false;
        const id = setInterval(() => { on = on || el.classList.contains('on'); }, 20);
        setTimeout(() => { clearInterval(id); resolve(on); }, 2500);
    }));
    expect(heard, 'nothing was played while setting the volume').toBe(true);
});

// ---- 5. back to nothing ----------------------------------------------------------------

test('the fifth press puts the whole row out', async ({ ux }) => {
    await ux.toMode('volume');
    await ux.press(120);
    await expect(ux.page.locator('#pill-mode')).toHaveText('ui idle');
    await ux.expectRowDark();
});

test('leaving a mode fades the pixel rather than cutting it', async ({ ux }) => {
    await ux.toMode('volume');
    await ux.page.waitForTimeout(400);          // let the fade-IN finish; it holds after that
    // Catch the fade out: sample across the press and count the levels on the way down.  A
    // hard cut shows the one steady level and then nothing; a ramp shows a handful.
    const fade = ux.watch(kVol, 1000);
    await ux.press(120);                        // the fifth press -> idle
    const seen = await fade;
    expect(seen.everLit).toBe(true);
    expect(seen.levels, 'the pixel was cut, not faded').toBeGreaterThan(2);
    await ux.expectRowDark();
});

// ---- 6. pairing ------------------------------------------------------------------------

test('ten seconds of hold opens pairing, and all five pixels breathe blue together',
    async ({ ux }) => {
    test.slow();                                    // one real ten-second hold, on purpose
    await ux.toMode('bell');

    const box = await ux.page.locator('#press').boundingBox();
    await ux.page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await ux.page.mouse.down();
    // The pill counts the hold, so the gesture is discoverable rather than a secret.
    await expect(ux.page.locator('#pill-mode')).toContainText('held', { timeout: 3000 });
    // It commits while the knob is still DOWN -- a gesture whose feedback arrives after you
    // let go is a gesture nobody finds.
    await expect(ux.page.locator('#pill-mode')).toContainText('ui pairing', { timeout: 14000 });

    // All five at once -- sampling synchronised breaths one at a time would compare five
    // different moments of the cycle and prove nothing.
    const row = await ux.watchMany([kBell, kAlarm, kClock, kVol, kBatt], 2400);
    row.per.forEach((w, n) => {
        expect(w.everLit, `status pixel ${n} was dark during pairing`).toBe(true);
        expect(w.peak.b, `status pixel ${n} is not blue`).toBeGreaterThan(w.peak.r + 20);
        expect(w.peak.b).toBeGreaterThan(w.peak.g + 20);
        expect(w.levels, `status pixel ${n} is not breathing`).toBeGreaterThan(4);
    });
    // "In sync" in its strongest form: at every one of those samples, all five agreed.
    expect(row.identical, 'the five pixels were not in phase').toBe(true);
    // The dial wash stays out of it.
    expect((await ux.watch(0, 300)).everLit).toBe(false);

    await ux.page.mouse.up();
    await ux.page.waitForTimeout(400);
    await expect(ux.page.locator('#pill-mode')).toContainText('ui pairing');  // release is spent

    await ux.press(120);                             // and a short press leaves
    await expect(ux.page.locator('#pill-mode')).toHaveText('ui idle');
    await ux.expectRowDark();
});

test('pairing obeys the same five seconds as every other mode', async ({ ux }) => {
    // There is ONE timeout now.  Pairing used to have two minutes of its own, which is a
    // second rule to learn about a control that has no labels (§6.6c, changed 2026-08-15).
    await ux.cli('ui knob pair 400');                // the ten seconds, shortened
    const box = await ux.page.locator('#press').boundingBox();
    await ux.page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await ux.page.mouse.down();
    await expect(ux.page.locator('#pill-mode')).toContainText('ui pairing', { timeout: 4000 });

    // A finger on the knob is input, so holding it does NOT time out underneath you...
    await ux.page.waitForTimeout(6000);
    await expect(ux.page.locator('#pill-mode')).toContainText('ui pairing');
    await ux.page.mouse.up();

    // ... and once you let go, the same five seconds apply.  Still there at three.
    await ux.page.waitForTimeout(3000);
    await expect(ux.page.locator('#pill-mode')).toContainText('ui pairing');
    await expect(ux.page.locator('#pill-mode')).toHaveText('ui idle', { timeout: 6000 });
    await ux.expectRowDark();
});
