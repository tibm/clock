// CASE 12 -- the user experience, mode by mode (README §12, FIRMWARE.md §6.6).
//
// One press is one mode, each mode lights the pixel whose icon names it, and the pattern on
// that pixel is a sentence: breathing white asks a question, blinking red states a fact,
// steady white means you are editing, three red flashes mean no.  The hands are the readout
// throughout -- including in the two modes where what they show is not a time.
//
// Chain order (FIRMWARE.md §9.2): 0-1 dial, 2 bell, 3 alarm, 4 clock, 5 vol, 6 batt.
'use strict';

const { test, expect, angleDiff } = require('./harness');

const kBell = 2, kAlarm = 3, kClock = 4, kVol = 5, kBatt = 6;

const hourDeg = (h, m) => (((h % 12) * 3600 + m * 60) / 43200) * 360;
const minuteDeg = (h, m) => ((m * 60) / 3600) * 360;

// Wait for the hands to arrive somewhere, in the dial's own degrees.
async function expectHands(ux, h, m, tol = 4) {
    await expect.poll(async () => {
        const at = await ux.hands();
        return Math.max(angleDiff(at.h, h), angleDiff(at.m, m));
    }, { timeout: 25000, message: `the hands never reached ${h.toFixed(1)}° / ${m.toFixed(1)}°` })
        .toBeLessThan(tol);
}

// ---- 1. bell: is the alarm on? ---------------------------------------------------------

test('bell, disarmed: the pixel breathes white and the hands stand at 12:00', async ({ ux }) => {
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

    // ... and the plainest thing a pair of hands can say.
    await expectHands(ux, 0, 0);
});

test('bell: clockwise arms and the pixel blinks red, anticlockwise disarms', async ({ ux }) => {
    await ux.toMode('bell');
    await expect(ux.page.locator('#c-alarm')).toContainText('off');

    await ux.turn(1);                       // one detent clockwise is enough -- it is a direction
    await expect(ux.page.locator('#c-alarm')).toContainText('armed');

    const bell = await ux.watch(kBell, 1500);
    expect(bell.peak.r).toBeGreaterThan(40);
    expect(bell.peak.g).toBe(0);
    expect(bell.peak.b).toBe(0);
    // A blink is square: one brightness, on and off, nothing in between.
    expect(bell.levels, 'a blink has hard edges').toBe(1);
    expect(bell.everDark).toBe(true);
    expect(bell.duty, 'fast blink, not a slow one').toBeGreaterThan(0.2);
    expect(bell.duty).toBeLessThan(0.8);

    await ux.turn(-1);
    await expect(ux.page.locator('#c-alarm')).toContainText('off');
});

test('bell: armed, the hands show the alarm time; disarmed, back to 12:00', async ({ ux }) => {
    await ux.home();
    await ux.toMode('bell');
    await ux.turn(1);
    await expect(ux.page.locator('#c-alarm')).toContainText('07:00 armed');
    await expectHands(ux, hourDeg(7, 0), minuteDeg(7, 0));

    await ux.turn(-1);
    await expect(ux.page.locator('#c-alarm')).toContainText('off');
    await expectHands(ux, 0, 0);
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
    const armed = await ux.watch(kAlarm, 1500);
    expect(armed.peak.r).toBeGreaterThan(40);
    expect(armed.peak.g).toBe(0);
    expect(armed.levels, 'armed blinks').toBe(1);
});

test('alarm: a slow turn is one minute, a fast one covers hours', async ({ ux }) => {
    await ux.toMode('alarm');
    await expect(ux.page.locator('#c-alarm')).toContainText('07:00');

    // ux.turn() waits 45 ms between detents, which is two ui ticks -- as slow as a hand.
    await ux.turn(3);
    await expect(ux.page.locator('#c-alarm')).toContainText('07:03');

    // The same knob, spun: 40 counts arriving inside one 20 ms poll is far past slow_max, so
    // the curve multiplies them.  This is the difference between setting 07:05 and winding
    // round to the evening, and it is the ONLY difference (FIRMWARE.md §6.6).
    await ux.cli('sim knob 40');
    await expect.poll(async () => {
        const t = await ux.text('c-alarm');
        const [h, m] = t.split(' ')[0].split(':').map(Number);
        return h * 60 + m;
    }, { timeout: 5000, message: 'a fast spin moved the alarm no further than a slow one' })
        .toBeGreaterThan(7 * 60 + 30);
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

test('pairing ignores the five-second timeout that every other mode obeys', async ({ ux }) => {
    await ux.cli('ui knob pair 400');                // the ten seconds, shortened
    const box = await ux.page.locator('#press').boundingBox();
    await ux.page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await ux.page.mouse.down();
    await expect(ux.page.locator('#pill-mode')).toContainText('ui pairing', { timeout: 4000 });
    await ux.page.mouse.up();

    await ux.page.waitForTimeout(7000);              // well past the 5 s every mode has
    await expect(ux.page.locator('#pill-mode')).toContainText('ui pairing');
    await ux.press(120);
    await expect(ux.page.locator('#pill-mode')).toHaveText('ui idle');
});
