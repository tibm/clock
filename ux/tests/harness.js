// The rig every spec runs against: a real clocksim, a real uxapp.py, a real page.
//
// One fresh pair per TEST, not per file.  clocksim starts in ~50 ms and the isolation is
// worth far more than the time: a suite where test 7 only passes because test 6 homed the
// hands is a suite that tells you nothing when it goes red.
//
// The rules this file exists to enforce (README.md in this directory says why):
//   * every gesture is a real DOM event on a real control, dispatched by the browser;
//   * every assertion reads what the PAGE renders, which is only ever what arrived in a
//     `state` frame from the firmware;
//   * nothing here speaks to clocksim directly.  There is no socket in this file, no
//     evaluate() that pokes app.js internals, and no way to make the dial say something the
//     firmware did not say.
'use strict';

const { test: base, expect } = require('@playwright/test');
const { spawn } = require('node:child_process');
const net = require('node:net');
const path = require('node:path');
const fs = require('node:fs');
const os = require('node:os');

const ROOT = path.resolve(__dirname, '..', '..');
const SIM = path.join(ROOT, 'firmware', 'build', 'host-dev', 'apps', 'clocksim', 'clocksim');
const UXAPP = path.join(ROOT, 'ux', 'uxapp.py');

// ---- processes ---------------------------------------------------------------------------

// Ports, without the race.  Asking the kernel for port 0 twice in two workers can hand out
// the SAME number -- neither has bound it yet -- and the loser then quietly attaches to the
// winner's clocksim, which fails later and somewhere else.  So each worker owns a disjoint
// block and walks it; the only thing we ask the kernel is whether a given port is free.
const WORKER = parseInt(process.env.TEST_WORKER_INDEX || '0', 10);
const BLOCK = 400;
let seq = 0;

function isFree(port) {
    return new Promise((resolve) => {
        const s = net.createServer();
        s.once('error', () => resolve(false));
        s.listen(port, '127.0.0.1', () => s.close(() => resolve(true)));
    });
}

async function freePort() {
    for (let i = 0; i < BLOCK; i++) {
        const p = 41000 + WORKER * BLOCK + ((seq++) % BLOCK);
        if (await isFree(p)) return p;
    }
    throw new Error(`worker ${WORKER} has no free port in its block`);
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function waitForTcp(port, timeoutMs, what) {
    const deadline = Date.now() + timeoutMs;
    for (;;) {
        const ok = await new Promise((resolve) => {
            const s = net.connect({ port, host: '127.0.0.1' });
            s.on('connect', () => { s.destroy(); resolve(true); });
            s.on('error', () => { s.destroy(); resolve(false); });
        });
        if (ok) return;
        if (Date.now() > deadline) throw new Error(`${what} never came up on 127.0.0.1:${port}`);
        await sleep(25);
    }
}

class Rig {
    constructor() {
        this.sim = null;
        this.ux = null;
        this.simLog = [];
        this.uxLines = [];
        this.simPort = 0;
        this.httpPort = 0;
        this.nvs = '';
    }

    // `extra` goes on clocksim's command line.  Two flags matter here:
    //
    //   --no-home   the real clock homes the moment it powers up (§6.1), and this suite does
    //               not want nine seconds of sweeping before every case.  The spec that is
    //               ABOUT boot homing turns it back on with `test.use({ simArgs: [] })`.
    //   --nvs       persistent settings, one file per rig.  Calibration survives a reboot on
    //               purpose, so a shared file would carry one case's trim into the next.
    static async start(extra = ['--no-home']) {
        const r = new Rig();
        if (!fs.existsSync(SIM)) {
            throw new Error(
                `clocksim is not built.\n  cd ${path.join(ROOT, 'firmware')}` +
                `\n  cmake --preset host-dev && cmake --build --preset host-dev`);
        }
        r.simPort = await freePort();
        r.httpPort = await freePort();

        // stdin is a PIPE and stays open.  clocksim's console front-end reads stdin and quits
        // on EOF, so a child spawned with stdin closed exits the instant it starts -- the
        // failure looks like "the port never opened" and costs an hour if you have not seen
        // it before.
        r.nvs = path.join(os.tmpdir(), `clocksim-${WORKER}-${r.simPort}.nvs`);
        try { fs.unlinkSync(r.nvs); } catch { /* first run */ }
        r.sim = spawn(SIM, ['--ui-port', String(r.simPort), '--nvs', r.nvs, ...extra], {
            stdio: ['pipe', 'pipe', 'pipe'],
            cwd: ROOT,
        });
        const keep = (buf) => {
            for (const line of String(buf).split('\n')) if (line.trim()) r.simLog.push(line);
            if (r.simLog.length > 400) r.simLog.splice(0, r.simLog.length - 400);
        };
        r.sim.stdout.on('data', keep);
        r.sim.stderr.on('data', keep);
        await waitForTcp(r.simPort, 8000, 'clocksim');
        // clocksim does not die when its port is taken -- it logs and carries on console-only,
        // and the connect above then succeeds against somebody ELSE's clocksim.  That is a
        // suite quietly testing the wrong process, so refuse to run.
        if (r.firmwareLog().includes('is taken')) {
            await r.stop();
            throw new Error(`port ${r.simPort} was taken; clocksim fell back to console-only`);
        }

        r.ux = spawn('python3', [UXAPP, '--sim-port', String(r.simPort),
                                 '--http-port', String(r.httpPort), '--no-browser'],
                     { stdio: ['ignore', 'pipe', 'pipe'], cwd: ROOT });
        const keepUx = (buf) => {
            for (const line of String(buf).split('\n')) if (line.trim()) r.uxLines.push(line);
        };
        r.ux.stdout.on('data', keepUx);
        r.ux.stderr.on('data', keepUx);
        await waitForTcp(r.httpPort, 8000, 'uxapp.py');
        return r;
    }

    uxLog() { return this.uxLines.join('\n'); }

    get url() { return `http://127.0.0.1:${this.httpPort}/`; }

    // clocksim's own stderr.  For diagnosis only -- assertions read the page, never this.
    firmwareLog() { return this.simLog.join('\n'); }

    async stop() {
        for (const p of [this.ux, this.sim]) {
            if (!p || p.exitCode !== null) continue;
            p.kill('SIGTERM');
        }
        if (this.sim && this.sim.stdin.writable) this.sim.stdin.end();
        await sleep(60);
        for (const p of [this.ux, this.sim]) {
            if (p && p.exitCode === null) p.kill('SIGKILL');
        }
        if (this.nvs) { try { fs.unlinkSync(this.nvs); } catch { /* never written */ } }
    }
}

// ---- reading the page --------------------------------------------------------------------
// Everything below reads rendered DOM.  `onState()` in app.js is the only thing that writes
// any of it, and it runs off the firmware's `state` frame.

// "rgb(12, 34, 56)" | "#000" | "" -> {r,g,b,lit}
function parseColor(css) {
    if (!css) return { r: 0, g: 0, b: 0, lit: false };
    const m = /rgba?\(([^)]+)\)/.exec(css);
    if (m) {
        const [r, g, b] = m[1].split(',').map((n) => parseFloat(n));
        return { r, g, b, lit: r + g + b > 0 };
    }
    if (/^#0{3,8}$/.test(css.trim())) return { r: 0, g: 0, b: 0, lit: false };
    const h = /^#([0-9a-f]{6})$/i.exec(css.trim());
    if (h) {
        const v = parseInt(h[1], 16);
        return { r: v >> 16, g: (v >> 8) & 255, b: v & 255, lit: v > 0 };
    }
    return { r: 0, g: 0, b: 0, lit: false };
}

const norm360 = (d) => ((d % 360) + 360) % 360;

// The one number this file is allowed to know about the mechanism, and it is not a guess:
// the `hello` frame carries usteps_per_rev, and the MECHANISM card prints it.
const kRev = 17280;
const degOf = (usteps) => norm360(usteps * 360 / kRev);

// After homing, a hand sits a little SHORT of where the firmware thinks it is: the rising
// edge of the index mark is half a mark before its centre, and motion adopts zero there.
// The offset is systematic, identical for both hands, and is what a `motion zero` trim will
// take out on the bench (motion.cpp, Phase::FineHour).  Assertions on a physical angle
// therefore carry it as tolerance rather than pretending it is not there.
const kHomeOffsetDeg = 3.0;

// The smallest angle between two bearings, so 359.6 and 0.2 are 0.6 apart and not 359.4.
function angleDiff(a, b) {
    const d = norm360(a - b);
    return d > 180 ? 360 - d : d;
}

class Ux {
    constructor(page, rig) {
        this.page = page;
        this.rig = rig;
    }

    // ---- readouts -------------------------------------------------------------------
    // Where a hand physically points, straight off the SVG the state frame rotated.
    async hand(which) {
        const sel = `#plate g.hand.${which === 'h' ? 'hour' : which === 'm' ? 'minute' : which}`;
        const t = await this.page.locator(sel).getAttribute('transform');
        const m = /rotate\(\s*(-?[\d.]+)/.exec(t || '');
        if (!m) throw new Error(`no rotate() on ${sel}: ${t}`);
        return norm360(parseFloat(m[1]));
    }

    async hands() { return { h: await this.hand('h'), m: await this.hand('m') }; }

    // How far the whole cube is turned on screen, straight off the group the IMU rotates.
    async plateYaw() {
        const t = await this.page.locator('#plate-rot').getAttribute('transform');
        const m = /rotate\(\s*(-?[\d.]+)/.exec(t || '');
        return m ? parseFloat(m[1]) : 0;
    }

    // What the FIRMWARE thinks it commanded, in microsteps.  Unwrapped int32, so reduce it
    // before comparing: -7200 and 10080 are the same place on the dial.
    async usteps() {
        const t = await this.text('m-pos');
        const [h, m] = t.split('/').map((s) => parseInt(s.trim(), 10));
        const wrap = (v) => ((v % kRev) + kRev) % kRev;
        return { h: wrap(h), m: wrap(m), rawH: h, rawM: m };
    }

    // The swatch strip is the app's pixel readout: one <i> per pixel, coloured from px[i].
    async pixel(i) {
        const css = await this.page.locator(`#swatches i[data-px="${i}"]`)
            .evaluate((e) => e.style.background);
        return parseColor(css);
    }

    async pixels() {
        const n = await this.page.locator('#swatches i').count();
        const out = [];
        for (let i = 0; i < n; i++) out.push(await this.pixel(i));
        return out;
    }

    // Every status pixel ANIMATES, so a single read is a coin toss: a breathing pixel is
    // genuinely dark twice a cycle and a blinking one is dark more than half the time.  The
    // only honest way to assert on a pattern is to watch it for a while, so this samples the
    // same swatch `pixel()` reads -- rendered DOM, written only by a state frame -- every
    // 10 ms and reports what the pixel DID:
    //
    //   peak      the brightest sample                (is it lit at all, and what colour)
    //   levels    how many distinct non-zero values   (1 = square edges, many = a curve)
    //   everDark  did it reach zero                   (breathe/blink vs solid)
    //   duty      fraction of samples lit             (blink 45 %, breathe ~2/3, solid 1)
    //
    // So `levels === 1 && everDark` is a blink, `levels > 4 && everDark` is a breath, and
    // `levels === 1 && !everDark` is solid -- which is exactly the distinction the spec
    // makes and the one a single read cannot see.
    async watch(i, ms = 1500) {
        return (await this.watchMany([i], ms)).per[0];
    }

    // The same, for several pixels AT ONCE -- which is the only way to ask whether they are
    // in phase.  Sampling five synchronised breaths one after another tells you nothing: the
    // second window lands somewhere else in the cycle and the peaks disagree for a reason
    // that has nothing to do with the firmware.  `identical` is the strong form of "in sync":
    // at every single sample, all of them were showing exactly the same thing.
    async watchMany(list, ms = 1500) {
        const raw = await this.page.evaluate(([pxs, dur]) => new Promise((resolve) => {
            const els = pxs.map((p) => document.querySelector(`#swatches i[data-px="${p}"]`));
            const out = [];
            const id = setInterval(() => out.push(els.map((e) => e.style.background || '')), 10);
            setTimeout(() => { clearInterval(id); resolve(out); }, dur);
        }), [list, ms]);
        const sum = (c) => c.r + c.g + c.b;
        const per = list.map((_, k) => {
            const cols = raw.map((row) => parseColor(row[k]));
            return {
                peak: cols.reduce((a, c) => (sum(c) > sum(a) ? c : a),
                                  { r: 0, g: 0, b: 0, lit: false }),
                levels: new Set(cols.filter((c) => c.lit).map(sum)).size,
                everDark: cols.some((c) => !c.lit),
                everLit: cols.some((c) => c.lit),
                duty: cols.filter((c) => c.lit).length / (cols.length || 1),
            };
        });
        return { per, identical: raw.every((row) => row.every((v) => v === row[0])),
                 samples: raw.length };
    }

    // Which WAY the hands went, which an angle cannot tell you.  350° -> 10° is +20 or -340
    // and nothing on the dial distinguishes them; `#m-pos` is the unwrapped microstep count,
    // so a sequence of those settles it.  Same rendered DOM `usteps()` reads, sampled every
    // 10 ms -- and stoppable, because a wind takes as long as it takes.
    //
    //   h.min / m.min   the most NEGATIVE single-sample step  (0 if it never went backwards)
    //   h.max / m.max   ... and the most positive
    //   h.net / m.net   where it ended up, less where it started
    //   angles          every sample as a dial position, 0..kRev, for "did it ever go there"
    // The first and last samples are taken HERE, synchronously, not left to the timer: a
    // setInterval tick is best-effort, and a browser busy with the command that starts the
    // move can delay the first one past it.  The window would then open a tick into the
    // motion, and "how far did it travel" comes out short by exactly that tick.
    async startHandWatch() {
        await this.page.evaluate(() => {
            const read = () => document.querySelector('#m-pos').textContent;
            window.__handSamples = [read()];
            window.__handWatch = setInterval(() => window.__handSamples.push(read()), 10);
        });
    }

    async stopHandWatch() {
        const raw = await this.page.evaluate(() => {
            clearInterval(window.__handWatch);
            window.__handSamples.push(document.querySelector('#m-pos').textContent);
            return window.__handSamples || [];
        });
        const pts = raw
            .map((t) => t.split('/').map((s) => parseInt(s.trim(), 10)))
            .filter(([h, m]) => Number.isFinite(h) && Number.isFinite(m))
            .map(([h, m]) => ({ h, m }));
        if (pts.length < 2) throw new Error(`the hand watch caught ${pts.length} samples`);
        const stats = (k) => {
            let min = 0, max = 0;
            for (let i = 1; i < pts.length; i++) {
                const d = pts[i][k] - pts[i - 1][k];
                if (d < min) min = d;
                if (d > max) max = d;
            }
            return { min, max, net: pts[pts.length - 1][k] - pts[0][k] };
        };
        const wrap = (v) => ((v % kRev) + kRev) % kRev;
        return {
            h: stats('h'),
            m: stats('m'),
            samples: pts.length,
            angles: pts.map((p) => ({ h: wrap(p.h), m: wrap(p.m) })),
        };
    }

    // Where the hands are once they have stopped moving, and it is the *start* of a
    // measurement, so it has to be exact.
    //
    // Both values come out of ONE read, and the position returned is the one from the read
    // that satisfied the wait -- not a fresh one afterwards.  Reading `#m-pos` and
    // `#pill-motion` separately lets them come from different state frames, and a position
    // taken from the frame before a hand stopped, next to an `idle` from the frame after it,
    // is a resting position six microsteps short of where the hand actually is. That is
    // exactly enough to make a test that measures 24 revolutions from it fail by six.
    async resting() {
        let last = null;
        await expect.poll(async () => {
            const [pos, motion] = await this.page.evaluate(() => [
                document.querySelector('#m-pos').textContent,
                document.querySelector('#pill-motion').textContent,
            ]);
            const still = motion === 'motion idle' && pos === last;
            last = pos;
            return still;
        }, { timeout: 30000, message: 'the hands never stopped moving' }).toBe(true);
        const [h, m] = last.split('/').map((s) => parseInt(s.trim(), 10));
        const wrap = (v) => ((v % kRev) + kRev) % kRev;
        return { h: wrap(h), m: wrap(m), rawH: h, rawM: m };
    }

    // The whole row went out and stayed out.  A fade takes ~250 ms, so "dark" is a thing you
    // wait for, not a thing you read (FIRMWARE.md §6.6a).
    async expectRowDark(ms = 600) {
        const all = [0, 1, 2, 3, 4, 5, 6];
        await expect.poll(async () => (await this.watchMany(all, 60)).per
                              .map((p, i) => (p.everLit ? i : -1)).filter((i) => i >= 0),
                          { timeout: 6000, message: 'a pixel was still lit' }).toEqual([]);
        const w = await this.watchMany(all, ms);
        expect(w.per.map((p, i) => (p.everLit ? i : -1)).filter((i) => i >= 0),
               'pixels still lit while idle').toEqual([]);
    }

    async text(id) { return (await this.page.locator(`#${id}`).textContent()).trim(); }

    // The per-unit trim, off the calibration sliders -- which are painted from the state
    // frame, so this is what the FIRMWARE holds and not what was last dragged.
    async zeros() {
        return {
            h: parseInt(await this.page.locator('#r-zeroh').inputValue(), 10),
            m: parseInt(await this.page.locator('#r-zerom').inputValue(), 10),
        };
    }

    // "3 · +12" -> {count: 3, last: 12}; "0" -> {count: 0, last: 0}
    async trims() {
        const t = await this.text('m-trims');
        const m = /^(\d+)(?:\s*·\s*([+-]?\d+))?$/.exec(t);
        return m ? { count: +m[1], last: m[2] ? +m[2] : 0 } : { count: 0, last: 0 };
    }

    // "07:38:04" -> {h, m, s}.  The page's own clock pill, from chrono's snapshot.
    async clock() {
        const t = await this.text('pill-clock');
        const m = /(\d{2}):(\d{2}):(\d{2})/.exec(t);
        if (!m) return null;
        return { h: +m[1], m: +m[2], s: +m[3], paused: t.includes('⏸') };
    }

    // Whether a state-coloured button is showing ok (green) / go (blue) / bad (red).
    async buttonState(id) {
        const cls = await this.page.locator(`#${id}`).getAttribute('class');
        for (const c of ['ok', 'go', 'bad']) if (cls.split(/\s+/).includes(c)) return c;
        return '';
    }

    async logText() { return await this.page.locator('#log').innerText(); }

    // ---- gestures -------------------------------------------------------------------
    // All of these are ordinary browser input.  Nothing writes to the socket by hand.

    async press(ms = 150) {
        const box = await this.page.locator('#press').boundingBox();
        await this.page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
        await this.page.mouse.down();
        await this.page.waitForTimeout(ms);
        await this.page.mouse.up();
    }

    // One notch of the wheel over the knob is one detent, which the encoder delivers as 4
    // PCNT counts.
    //
    // The gap between them is not politeness, it is the movement: a setting may not run
    // faster than the hands can draw it, and what arrives faster is DROPPED rather than
    // banked (§6.6d).  One minute of dial is 288 microsteps, which at the shipping 6000
    // usteps/s is 48 ms -- so a "one detent, one minute" case has to turn slower than that
    // or it is asking the dial for something no dial can show, and the answer is correctly
    // fewer minutes.  140 ms is a brisk turn with a lot of room: the margin is there because
    // a LOADED machine delays the ui's poll, several detents then arrive in one read, and
    // only one detent's worth of them survives -- which is correct firmware behaviour and a
    // flaky test.  (Cases that want a spin faster than the hands say so explicitly:
    // `sim knob n`, or `sim knob n over ms`.)
    async turn(detents, gapMs = 140) {
        const box = await this.page.locator('#knob').boundingBox();
        await this.page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
        for (let i = 0; i < Math.abs(detents); i++) {
            await this.page.mouse.wheel(0, detents > 0 ? 120 : -120);
            await this.page.waitForTimeout(gapMs);  // app.js coalesces counts on a 33 ms timer
        }
        await this.page.waitForTimeout(150);
    }

    // The same detent through the keyboard, which is the other documented way to nudge it.
    async arrowTurn(detents) {
        await this.page.locator('h1').click();   // somewhere that is not the CLI box
        const key = detents > 0 ? 'ArrowRight' : 'ArrowLeft';
        for (let i = 0; i < Math.abs(detents); i++) {
            await this.page.keyboard.press(key);
            await this.page.waitForTimeout(140);
        }
        await this.page.waitForTimeout(120);
    }

    // Drag a hand round the dial -- reaching through the glass, not telling the firmware.
    async dragHand(which, deg) {
        const sel = `#plate g.hand.${which === 'h' ? 'hour' : 'minute'}`;
        const plate = await this.page.locator('#plate').boundingBox();
        const cx = plate.x + plate.width / 2;
        const cy = plate.y + plate.height / 2;
        const grab = await this.page.locator(sel).boundingBox();
        await this.page.mouse.move(grab.x + grab.width / 2, grab.y + grab.height / 2);
        await this.page.mouse.down();
        const r = Math.min(plate.width, plate.height) * 0.3;
        // `deg` is in the DIAL's frame, which is the frame `sim hand` speaks.  The plate on
        // screen is turned by the IMU's yaw, so the SCREEN angle to aim at is deg + yaw --
        // exactly the correction app.js takes back out at the other end.
        const yaw = await this.plateYaw();
        for (let i = 1; i <= 6; i++) {
            const a = (yaw + deg * i / 6 - 90) * Math.PI / 180;
            await this.page.mouse.move(cx + r * Math.cos(a), cy + r * Math.sin(a));
            await this.page.waitForTimeout(45);
        }
        await this.page.mouse.up();
        await this.page.waitForTimeout(120);
    }

    // Move a slider by setting its value the way a drag would, then letting the page hear it.
    async slide(id, value) {
        await this.page.locator(`#${id}`).fill(String(value));
        await this.page.waitForTimeout(120);
    }

    // Click `home` and wait for the movement's own answer.
    //
    // Waiting only for green is not enough: after an earlier home the button is ALREADY
    // green, so this returned before the new run had started and the caller measured the
    // hands from the previous one.  Wait for the run to be live first.
    async home() {
        await this.page.locator('#btn-home').click();
        await expect(this.page.locator('#btn-home')).toHaveText('homing…', { timeout: 10000 });
        await expect(this.page.locator('#btn-home')).toHaveClass(/\bok\b/, { timeout: 45000 });
    }

    // The on-page CLI box.  It is a control on the page like any other, it dispatches the
    // same line the terminal would, and the firmware cannot tell the difference -- which is
    // why it is fair game for arranging a precondition exactly (a hand at 137.0 deg rather
    // than the scramble button's random one) but never for asserting anything.
    async cli(line) {
        await this.page.locator('#cli').fill(line);
        await this.page.locator('#cli').press('Enter');
        await this.page.locator('#cli').blur();
    }

    // Reach into the case and point a hand somewhere.  The firmware is NOT told; the gap is
    // exactly what `motion home` exists to discover.
    async placeHand(which, deg) { await this.cli(`sim hand ${which} ${deg}`); }

    // Just the mode word: "setalarm" out of "ui setalarm · 4.9s".
    async mode() {
        return (await this.text('pill-mode')).replace(/^ui\s+/, '').split('·')[0].trim();
    }

    // Press until the HSM is in `want`.
    //
    // Reading the pill straight after a click still shows the PREVIOUS mode, and that is not
    // a bug: the press has to reach the firmware, the ui AO has to poll ENC_SW, and a state
    // frame has to come back -- fifty-odd milliseconds all told.  A helper that chose its
    // next press from that stale text overshot the mode it was asked for and the test then
    // edited something else entirely, which looked exactly like a firmware fault.  So: press,
    // wait for the mode to actually MOVE, then decide.
    async toMode(want) {
        for (let i = 0; i < 8; i++) {
            const at = await this.mode();
            if (at === want) return;
            await this.press(120);
            await expect.poll(() => this.mode(), { timeout: 5000 }).not.toBe(at);
        }
        throw new Error(`never reached ${want}; stuck at ${await this.mode()}`);
    }

    async waitIdleMode() {
        await expect(this.page.locator('#pill-mode')).toHaveText(/ui idle/, { timeout: 9000 });
    }

    // Does the DIAL agree with the CLOCK?  Both read off the page, both fed by the firmware,
    // and the expected angle worked out from first principles rather than from anything the
    // app said about it: the hour hand is continuous, so 07:30 is halfway between 7 and 8.
    async dialTimeError() {
        const c = await this.clock();
        if (!c) return 999;
        const hands = await this.hands();
        const hourDeg = (((c.h % 12) * 3600 + c.m * 60 + c.s) / 43200) * 360;
        const minDeg = ((c.m * 60 + c.s) / 3600) * 360;
        return Math.max(angleDiff(hands.h, hourDeg), angleDiff(hands.m, minDeg));
    }

    async expectDialShowsTime(tol = kHomeOffsetDeg + 1.0) {
        await expect.poll(() => this.dialTimeError(),
                          { timeout: 25000, message: 'the hands never reached the time on the pill' })
            .toBeLessThan(tol);
    }
}

// ---- the fixture --------------------------------------------------------------------------

const test = base.extend({
    // What to add to clocksim's command line, so a spec can ask for a different clock:
    // `test.use({ simArgs: [] })` gets one that homes on boot, the way the product does.
    simArgs: [['--no-home'], { option: true }],

    ux: async ({ page, simArgs }, use, testInfo) => {
        const rig = await Rig.start(simArgs);
        // Everything from here on must reach the stop() in `finally`.  A throw during setup
        // that leaks a clocksim leaks its PORT too, and the next test's attach then fails for
        // a reason that has nothing to do with the next test -- one flake becomes a cascade
        // and the report blames the wrong thing.
        try {
            const ux = new Ux(page, rig);
            const pageErrors = [];
            page.on('pageerror', (e) => pageErrors.push(String(e)));
            // Kept even though attaching is reliable now: the last time it was not, the cause
            // was clocksim being killed by SIGPIPE two hundred milliseconds earlier, and the
            // only visible symptom was a page that said "disconnected".  Whatever the next
            // cause turns out to be, the answer will be in one of these four.
            const console_ = [];
            page.on('console', (m) => console_.push(`${m.type()}: ${m.text()}`));
            page.on('response', (r) => console_.push(`<- ${r.status()} ${r.url()}`));
            page.on('requestfailed', (r) =>
                console_.push(`XX ${r.url()} ${r.failure() && r.failure().errorText}`));
            page.on('websocket', (w) => {
                console_.push(`WS open ${w.url()}`);
                w.on('socketerror', (e) => console_.push(`WS socketerror ${e}`));
                w.on('close', () => console_.push('WS close'));
            });
            await page.goto(rig.url);
            // `hello` has landed once the firmware's own pixel names are on screen.
            try {
                await expect(page.locator('#swatches i')).toHaveCount(7, { timeout: 15000 });
            } catch {
                const conn = await page.locator('#conn').textContent().catch(() => '(no #conn)');
                throw new Error(
                    `never attached to clocksim on ${rig.simPort} (http ${rig.httpPort})\n` +
                    `  #conn says: ${conn}\n` +
                    `  browser console: ${console_.join(' | ') || '(silent)'}\n` +
                    `  page errors: ${pageErrors.join(' | ') || '(none)'}\n` +
                    `  clocksim said:\n${rig.firmwareLog()}\n` +
                    `  uxapp said:\n${rig.uxLog()}`);
            }
            await expect(page.locator('#conn')).toHaveClass(/up/);
            // app.js arms `unsafe` 300 ms in; a gated command sent before that is Denied.
            await page.waitForTimeout(400);

            await use(ux);

            if (pageErrors.length) {
                testInfo.attach('page errors', { body: pageErrors.join('\n\n') });
            }
            if (testInfo.status !== testInfo.expectedStatus) {
                testInfo.attach('clocksim log', { body: rig.firmwareLog() });
                testInfo.attach('ux console', { body: await ux.logText().catch(() => '') });
            }
            if (pageErrors.length) throw new Error(`the page threw:\n${pageErrors.join('\n')}`);
        } finally {
            await rig.stop();
        }
    },
});

module.exports = { test, expect, angleDiff, norm360, parseColor, kRev, degOf, kHomeOffsetDeg };
