// CASE 8 -- the rear toggle and the top tap: the two inputs that are not the knob.
'use strict';

const { test, expect } = require('./harness');

const kBell = 2;

test('the rear toggle disables the radios, and the label names the STATE', async ({ ux }) => {
    await expect(ux.page.locator('#t-radio')).toHaveText('radios on');
    await ux.page.locator('#t-radio').click();
    await expect(ux.page.locator('#t-radio')).toHaveText('radios OFF');
    await expect(ux.page.locator('#t-radio')).toHaveClass(/\bon\b/);
    await ux.page.locator('#t-radio').click();
    await expect(ux.page.locator('#t-radio')).toHaveText('radios on');
});

test('the toggle follows a change it did not make', async ({ ux }) => {
    await ux.cli('sim radio on');            // "on" = the switch is asserted = radios OFF
    await expect(ux.page.locator('#t-radio')).toHaveText('radios OFF');
    await ux.cli('sim radio off');
    await expect(ux.page.locator('#t-radio')).toHaveText('radios on');
});

test('a top tap is acknowledged on the bell pixel long enough to see', async ({ ux }) => {
    // Tap-to-snooze has nothing to snooze yet, so the acknowledgement IS the feature: a tap
    // has to look like it did something.  Sample fast, because a flash nobody can see is the
    // failure this is looking for.
    const lit = ux.page.evaluate(() => new Promise((resolve) => {
        const el = document.querySelector('#swatches i[data-px="2"]');
        let best = 0, on = 0, t0 = 0;
        const id = setInterval(() => {
            const dark = !el.style.background || el.style.background === 'rgb(0, 0, 0)';
            if (!dark) {
                if (!on) { on = 1; t0 = performance.now(); }
                best = Math.max(best, performance.now() - t0);
            } else if (on) { on = 0; }
        }, 5);
        setTimeout(() => { clearInterval(id); resolve(best); }, 2500);
    }));
    await ux.page.waitForTimeout(120);
    await ux.page.locator('button[data-cmd="sim tap"]').click();
    const ms = await lit;
    expect(ms, 'the bell pixel was never lit after a tap').toBeGreaterThan(0);
    expect(ms, 'the tap flash was too short to see').toBeGreaterThan(100);
});

test('a tap does not disturb the mode or the hands', async ({ ux }) => {
    await ux.home();
    const before = await ux.usteps();
    await ux.page.locator('button[data-cmd="sim tap"]').click();
    await ux.page.waitForTimeout(800);
    await expect(ux.page.locator('#pill-mode')).toContainText('ui idle');
    expect(await ux.usteps()).toMatchObject({ h: before.h, m: before.m });
});
