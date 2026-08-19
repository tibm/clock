// The page is a display and an input device.  It holds no clock logic of any kind: every
// number on screen came out of a `state` frame, and every gesture leaves as a CLI line.
// If you catch this file computing where a hand ought to be, that is the bug.

import { build, polar } from '/web/clockface.js';

const $ = (id) => document.getElementById(id);

let G = null;          // geometry.json
let face = null;       // the SVG handles clockface.js hands back
let ws = null;
let nextId = 1;
const pending = new Map();

// ---- transport -------------------------------------------------------------------------

function send(line) {
    if (ws && ws.readyState === WebSocket.OPEN) ws.send(line);
}

// The rows that `unsafe` gates (§9.6).  The window is 60 s of REAL time from the last such
// command, and a background tab has its timers throttled to a minute or more -- so the
// periodic refresh below is not enough on its own, and the symptom is a `home` that works
// when you are watching and is silently Denied when you come back to the tab.  Arming it
// immediately before the command that needs it costs one line and cannot lapse.
const kNeedsUnsafe = /^(motion (home|goto|step)|ui (led|wake)|sys reboot)\b/;

// Fire-and-forget with a reply: `res` comes back tagged so we can print it next to the
// command that caused it.
function cmd(line, quiet = false) {
    if (kNeedsUnsafe.test(line)) send('unsafe on');
    const id = nextId++;
    pending.set(id, { line, quiet });
    if (!quiet) log('cmd', '', '> ' + line);
    send(`#${id} ${line}`);
}

function connect() {
    ws = new WebSocket(`ws://${location.host}/ws`);
    ws.onopen = () => setConn(true, 'attached');
    ws.onclose = () => { setConn(false, 'disconnected'); setTimeout(connect, 1200); };
    ws.onerror = () => setConn(false, 'error');
    ws.onmessage = (ev) => {
        let m;
        try { m = JSON.parse(ev.data); } catch { return; }
        switch (m.t) {
            case 'hello': onHello(m); break;
            case 'state': onState(m); break;
            case 'log': log(m.lvl, m.mod, m.msg); break;
            case 'res': onRes(m); break;
            case 'offline': setConn(false, m.msg); log('error', 'ux', m.msg); break;
        }
    };
}

function setConn(up, text) {
    const p = $('conn');
    p.classList.toggle('up', up);
    p.innerHTML = '<i></i>' + text;
}

function onHello(m) {
    setConn(true, `${m.app} · ${m.board}`);
    // Pixel names come from the firmware so the swatch strip cannot drift from the chain.
    const sw = $('swatches');
    sw.innerHTML = '';
    m.px_names.forEach((name, i) => {
        const d = document.createElement('div');
        d.className = 'sw';
        d.innerHTML = `<i data-px="${i}"></i><span>${i} ${name}</span>`;
        sw.appendChild(d);
    });
}

function onRes(m) {
    const p = pending.get(m.id);
    pending.delete(m.id);
    const quiet = p ? p.quiet : false;
    if (m.st !== 'ok' && !quiet) log('warn', '', `[${m.st}]`);
    else if (m.st !== 'ok') log('warn', '', `${p ? p.line : ''} → [${m.st}]`);
    for (const l of m.lines || []) if (!quiet || m.st !== 'ok') log('res', '', '  ' + l);
}

// ---- log ---------------------------------------------------------------------------------

const logEl = () => $('log');

function log(cls, mod, text) {
    const box = logEl();
    const atBottom = box.scrollHeight - box.scrollTop - box.clientHeight < 30;
    const d = document.createElement('div');
    d.className = cls;
    d.textContent = mod ? `${mod.padEnd(8)} ${text}` : text;
    box.appendChild(d);
    while (box.childElementCount > 500) box.removeChild(box.firstChild);
    if (atBottom) box.scrollTop = box.scrollHeight;
}

// ---- rendering ---------------------------------------------------------------------------

const rgbwCss = (p) => {
    // The W die is a separate warm-white emitter, so it adds to the RGB rather than
    // replacing it -- an all-W pixel must not render as black.
    const [r, g, b, w] = p;
    return `rgb(${Math.min(255, r + w)},${Math.min(255, g + w * 0.94)},${Math.min(255, b + w * 0.82)})`;
};
const lit = (p) => p[0] + p[1] + p[2] + p[3] > 0;

let uiFrozenUntil = 0;   // do not fight the user while they are dragging a slider
let plateYaw = 0;        // where the cube is pointing, so a hand drag can be un-rotated

// 48 microsteps to the degree.  The firmware speaks microsteps; a person calibrating a hand by
// eye is thinking in degrees, so the label says both.
const kUstepsPerDeg = 17280 / 360;
const zeroLabel = (v) => `${v > 0 ? '+' : ''}${v} · ${(v / kUstepsPerDeg).toFixed(2)}°`;

// A toggle is a REQUEST, not a mirror of the last frame.  Deciding what to send by reading
// the class that is currently painted loses the race: two clicks inside one round trip both
// read the old state, both send the same command, and the second one appears to do nothing.
// So a click records what it ASKED for, the button shows that immediately, and the state
// frames only take the paint back once they agree -- or after the request has clearly been
// lost.
const intent = new Map();          // id -> { want, until }
const kIntentMs = 1500;

// What the button is showing right now: the pending request if there is one, else the truth.
function shown(id, actual) {
    const w = intent.get(id);
    if (!w) return actual;
    if (w.want === actual || Date.now() > w.until) {
        intent.delete(id);
        return actual;
    }
    return w.want;
}

function request(id, want, line) {
    intent.set(id, { want, until: Date.now() + kIntentMs });
    cmd(line, true);
}

// Three-way status colour, driven by the firmware's own words rather than by this app's
// guess at them.  'ok' green, 'go' blue (in progress), 'bad' red.
function paintState(id, cls) {
    const b = $(id);
    for (const c of ['ok', 'go', 'bad']) b.classList.toggle(c, c === cls);
}

// A square turned by yaw needs (|cos| + |sin|) times its width; shrinking by the inverse
// keeps the corners inside the viewBox at every angle, and is exactly 1 at yaw 0.
const fitScale = (deg) => {
    const a = deg * Math.PI / 180;
    return 1 / (Math.abs(Math.cos(a)) + Math.abs(Math.sin(a)));
};

function onState(s) {
    face.hour.setAttribute('transform', `rotate(${s.hands.h})`);
    face.minute.setAttribute('transform', `rotate(${s.hands.m})`);

    // The cube itself, turned by the BNO085's yaw -- the whole plate, not the hands, so the
    // index window and the status LEDs go round with it the way they would on the shelf.
    if (s.imu.yaw !== plateYaw) {
        plateYaw = s.imu.yaw;
        face.rot.setAttribute('transform',
            `rotate(${plateYaw.toFixed(2)}) scale(${fitScale(plateYaw).toFixed(4)})`);
    }

    const two = (n) => String(n).padStart(2, '0');
    $('pill-clock').textContent = s.clock.valid
        ? `${two(s.clock.h)}:${two(s.clock.m)}:${two(s.clock.s)}${s.clock.follow ? '' : ' ⏸'}`
        : '--:--:--';
    $('pill-sim').textContent = `sim ${(s.ms / 1000).toFixed(1)} s`;
    $('pill-warp').textContent = `warp ${s.warp.toFixed(2)}×`;
    // The FSM's own word for what it is doing, not this app's guess at it.  Whether it is
    // homed is NOT repeated here -- the home button is the one place that says so.
    $('pill-motion').textContent = s.motion.phase
        ? `motion ${s.motion.state} · ${s.motion.phase}`
        : `motion ${s.motion.state}`;
    $('pill-motion').classList.toggle('up', s.motion.state === 'moving');
    $('pill-motion').classList.toggle('bad', s.motion.state === 'fault');
    // While the knob is DOWN the pill counts the hold instead of the timeout: ten seconds is
    // a long time to hold a button with no idea whether anything is happening, and the count
    // is the firmware's own (`ui.held`), not a timer this page started on mousedown.
    $('pill-mode').textContent = 'ui ' + s.ui.mode
        + (s.ui.held ? ` · held ${(s.ui.held / 1000).toFixed(1)}s`
                     : s.ui.idle_in ? ` · ${(s.ui.idle_in / 1000).toFixed(1)}s` : '');
    $('c-alarm').textContent = `${two(s.ui.alarm_h)}:${two(s.ui.alarm_m)} ${s.ui.armed ? 'armed' : 'off'}`;
    $('c-vol').textContent = `${s.ui.vol}%`;

    // Two buttons, one fact: whichever is true right now is the one that lights.
    const following = shown('btn-follow', s.clock.follow);
    paintState('btn-follow', following ? 'ok' : '');
    paintState('btn-release', following ? '' : 'bad');

    $('m-h').textContent = `${s.hands.h.toFixed(1)}°`;
    $('m-m').textContent = `${s.hands.m.toFixed(1)}°`;
    $('m-pos').textContent = `${s.hands.hp} / ${s.hands.mp}`;
    $('m-vel').textContent = `${s.hands.hv} / ${s.hands.mv}`;
    $('m-home').textContent = s.motion.home_ms ? `${(s.motion.home_ms / 1000).toFixed(1)} s` : '—';
    $('m-faults').textContent = s.motion.faults;
    // How many index crossings have corrected a hand, and by how much the last one did.  Zero
    // is the healthy reading on a clock that has just homed; a number that climbs slowly is a
    // movement being kept honest, and a number that jumps is one worth watching.
    $('m-trims').textContent = s.motion.trims
        ? `${s.motion.trims} · ${s.motion.trim > 0 ? '+' : ''}${s.motion.trim}` : '0';
    // Which of the twelve dots gravity says is at the top, and what that costs every target.
    // On an upright cube this reads "printed" and never moves, which is the point: the whole
    // mechanism is invisible until somebody turns the clock over.
    $('m-dial').textContent = s.motion.dial
        ? `${s.motion.dial}/12 · +${s.motion.dial * 30}°` : 'printed';
    // The button is the one place that says whether the hands are trustworthy, because it is
    // where you look when they are not.  Blue only while a run is actually in progress: a
    // colour that never changes is a colour nobody reads.
    paintState('btn-home',
        s.motion.state === 'homing' ? 'go' : (s.motion.homed && s.motion.state !== 'fault') ? 'ok' : 'bad');
    // The label stays a verb -- it is still the button that homes.  Only the colour, and the
    // ellipsis while a run is live, report state.
    $('btn-home').textContent = s.motion.state === 'homing' ? 'homing…' : 'home';

    $('opto-bar').style.width = `${(s.opto.n * 100).toFixed(1)}%`;
    $('opto-txt').textContent = `opto ${s.opto.n.toFixed(3)} ${s.opto.auto ? 'auto' : 'held'}`;
    face.optoGlow.setAttribute('opacity', Math.max(0, (s.opto.n - 0.15) / 0.85).toFixed(3));

    $('k-count').textContent = s.knob.count;
    $('k-sw').textContent = s.knob.sw ? 'DOWN' : 'up';

    // pixels
    document.querySelectorAll('.sw i').forEach((i) => {
        const p = s.px[+i.dataset.px];
        i.style.background = lit(p) ? rgbwCss(p) : '#000';
        i.style.boxShadow = lit(p) ? `0 0 9px ${rgbwCss(p)}` : 'none';
    });
    for (const led of face.leds) {
        const p = s.px[led.px];
        led.lens.setAttribute('fill', lit(p) ? rgbwCss(p) : '#0a0c0d');
        led.glow.setAttribute('fill', rgbwCss(p));
        led.glow.setAttribute('opacity', lit(p) ? 0.85 : 0);
    }
    for (const wash of face.dialWash) {
        const p = s.px[+wash.dataset.px];
        wash.setAttribute('fill', rgbwCss(p));
        wash.setAttribute('opacity', lit(p) ? 0.3 : 0);
    }

    // The wake COB is on the back of the cube, so it reads as a wash around the plate.
    const duty = Math.max(s.wake.warm, s.wake.cool) / 100;
    const warmth = s.wake.warm + s.wake.cool > 0
        ? s.wake.warm / (s.wake.warm + s.wake.cool) : 1;
    face.wake.setAttribute('fill',
        `rgb(255,${Math.round(190 + 40 * (1 - warmth))},${Math.round(120 + 110 * (1 - warmth))})`);
    face.wake.setAttribute('opacity', (duty * 0.75).toFixed(3));

    $('s-wake').textContent = `${s.wake.warm} / ${s.wake.cool} %`;
    const spk = $('s-spk');
    spk.textContent = s.spk.on ? `on · ${s.spk.vol}%` : 'off';
    spk.classList.toggle('on', s.spk.on);

    // Both toggles show the pending request until the firmware agrees with it, so a fast
    // second click is never decided from a stale frame.  The labels name the STATE rather
    // than the action -- "radio off" next to "radio OFF" was a difference of one shift key.
    const radioOff = shown('t-radio', s.radio_off);
    $('t-radio').classList.toggle('on', radioOff);
    $('t-radio').textContent = radioOff ? 'radios OFF' : 'radios on';
    const plugged = shown('t-plug', s.pwr.plugged);
    $('t-plug').classList.toggle('on', plugged);
    $('t-plug').textContent = plugged ? `plugged · ${s.pwr.soc}%` : `battery · ${s.pwr.soc}%`;
    // Two facts, one button: provisioned AND synced is what `net` will report.  Whether that
    // adds up to "the network owns the time" also depends on the rear toggle, and only the
    // firmware gets to decide it -- so the label reads `ui.locked`, not this button's state.
    const netUp = shown('t-net', s.clock.prov && s.clock.sync);
    $('t-net').classList.toggle('on', netUp);
    $('t-net').textContent = netUp ? (s.ui.locked ? 'network · clock locked' : 'network · radios off')
                                   : 'no network';

    if (Date.now() > uiFrozenUntil) {
        // The calibration is the firmware's, not the page's: it comes back from NVS at boot, so
        // a reloaded page shows what this unit is actually trimmed to rather than zero.
        $('r-zeroh').value = s.motion.zero_h;
        $('v-zeroh').textContent = zeroLabel(s.motion.zero_h);
        $('r-zerom').value = s.motion.zero_m;
        $('v-zerom').textContent = zeroLabel(s.motion.zero_m);
        $('r-yaw').value = Math.round(s.imu.yaw);
        $('v-yaw').textContent = `${s.imu.yaw.toFixed(0)}°`;
        $('r-vbat').value = s.pwr.mv;
        $('v-vbat').textContent = `${s.pwr.mv} mV`;
        $('v-warp').textContent = `${s.warp.toFixed(2)}×`;
    }
}

// ---- input -------------------------------------------------------------------------------

// Angle of a pointer event about an SVG's centre, in the firmware's convention.
function angleAt(svg, ev) {
    const r = svg.getBoundingClientRect();
    const dx = ev.clientX - (r.left + r.width / 2);
    const dy = ev.clientY - (r.top + r.height / 2);
    const deg = Math.atan2(dx, -dy) * 180 / Math.PI;
    return (deg + 360) % 360;
}

// A dragged knob is a continuous angle, so counts are accumulated and flushed on a timer:
// one CLI line per frame rather than one per pixel of mouse travel.
let knobAccum = 0;
let knobTimer = null;
function knobBy(counts) {
    knobAccum += counts;
    if (knobTimer) return;
    knobTimer = setTimeout(() => {
        knobTimer = null;
        const n = Math.trunc(knobAccum);
        knobAccum -= n;
        if (n) cmd(`sim knob ${n}`, true);
    }, 33);
}

function wireKnob() {
    const knob = $('knob');
    let last = null;
    let visual = 0;

    // Turning the knob is one gesture with three inputs -- drag, wheel, arrow keys -- and all
    // three have to move the same two things: the mark on the knob, and the count in the
    // firmware.  The arrows used to send counts without turning the mark, so a keyboard nudge
    // in `idle` (where a turn is correctly ignored) looked exactly like a dead key.
    const spin = (counts) => {
        visual += counts * 360 / 256;
        $('knob-dial').setAttribute('transform', `rotate(${visual})`);
        knobBy(counts);
    };

    knob.addEventListener('pointerdown', (e) => {
        knob.setPointerCapture(e.pointerId);
        last = angleAt(knob, e);
    });
    knob.addEventListener('pointermove', (e) => {
        if (last === null) return;
        const now = angleAt(knob, e);
        let d = now - last;
        if (d > 180) d -= 360;
        if (d < -180) d += 360;
        last = now;
        visual += d;
        $('knob-dial').setAttribute('transform', `rotate(${visual})`);
        knobBy(d * 256 / 360);
    });
    const drop = (e) => {
        if (last === null) return;
        last = null;
        knob.releasePointerCapture(e.pointerId);
    };
    knob.addEventListener('pointerup', drop);
    knob.addEventListener('pointercancel', drop);
    knob.addEventListener('wheel', (e) => {
        e.preventDefault();
        spin(Math.sign(e.deltaY) * 4);          // one detent per notch
    }, { passive: false });

    const press = $('press');
    const down = () => { press.classList.add('down'); cmd('sim press down'); };
    const up = () => { press.classList.remove('down'); cmd('sim press up'); };
    press.addEventListener('pointerdown', down);
    press.addEventListener('pointerup', up);
    press.addEventListener('pointerleave', () => {
        if (press.classList.contains('down')) up();
    });

    addEventListener('keydown', (e) => {
        // The CLI box owns every key while it has focus -- but a slider does not own the
        // arrows.  It used to: click any slider and the next arrow key moved THAT, silently,
        // and the knob appeared broken for the rest of the session.  The keys are documented
        // as the knob's, so they are the knob's, and preventDefault keeps the slider still.
        if (e.target.tagName === 'INPUT' && e.target.type !== 'range') return;
        if (e.key === 'ArrowRight') { e.preventDefault(); spin(4); }
        else if (e.key === 'ArrowLeft') { e.preventDefault(); spin(-4); }
        else if (e.code === 'Space' && !e.repeat) { e.preventDefault(); down(); }
    });
    addEventListener('keyup', (e) => {
        if (e.code === 'Space' && press.classList.contains('down')) up();
    });
}

// Dragging a hand is reaching into the case and moving it: it changes where the hand IS,
// not where the firmware thinks it is.  Which is exactly the error homing exists to find.
function wireHands() {
    const svg = $('plate');
    let dragging = null;
    let lastSent = 0;

    for (const g of [face.hour, face.minute]) {
        g.addEventListener('pointerdown', (e) => {
            e.preventDefault();
            dragging = g.dataset.hand;
            svg.setPointerCapture(e.pointerId);
        });
    }
    svg.addEventListener('pointermove', (e) => {
        if (!dragging) return;
        const now = performance.now();
        if (now - lastSent < 33) return;
        lastSent = now;
        // Screen angle, less the yaw the plate is drawn at: `sim hand` is in the dial's own
        // frame, and a tilted cube must not shift where you just put the hand.
        const deg = (angleAt(svg, e) - plateYaw + 360) % 360;
        cmd(`sim hand ${dragging} ${deg.toFixed(1)}`, true);
    });
    const drop = (e) => {
        if (!dragging) return;
        dragging = null;
        svg.releasePointerCapture(e.pointerId);
    };
    svg.addEventListener('pointerup', drop);
    svg.addEventListener('pointercancel', drop);
}

function wireControls() {
    for (const b of document.querySelectorAll('button[data-cmd]')) {
        b.addEventListener('click', () => cmd(b.dataset.cmd));
    }
    // Reaching into the case and moving a hand somewhere you did not choose: a fixed angle
    // stops being a scramble the second time you press it.
    for (const b of document.querySelectorAll('button[data-rand]')) {
        b.addEventListener('click', () =>
            cmd(`sim hand ${b.dataset.rand} ${(Math.random() * 360).toFixed(1)}`));
    }
    // The real time, off the machine you are sitting at.  Reading a clock and typing what it
    // says is an INPUT -- the same thing SNTP will be once there is a network (§7.4) -- so it
    // leaves as one `chrono time set`, and nothing here works out what the hands should do
    // about it.  Local time, not UTC: the dial has no timezone.
    $('btn-now').addEventListener('click', () => {
        const d = new Date();
        const two = (n) => String(n).padStart(2, '0');
        cmd(`chrono time set ${two(d.getHours())}:${two(d.getMinutes())}:${two(d.getSeconds())}`);
    });
    $('t-radio').addEventListener('click', () => {
        const want = !shown('t-radio', $('t-radio').classList.contains('on'));
        request('t-radio', want, `sim radio ${want ? 'on' : 'off'}`);
    });
    $('t-net').addEventListener('click', () => {
        const want = !shown('t-net', $('t-net').classList.contains('on'));
        request('t-net', want, `chrono net ${want ? 'both' : 'none'}`);
    });
    $('t-plug').addEventListener('click', () => {
        const want = !shown('t-plug', $('t-plug').classList.contains('on'));
        request('t-plug', want, want ? 'sim plug' : 'sim unplug');
    });
    $('btn-follow').addEventListener('click', () => intent.set('btn-follow',
        { want: true, until: Date.now() + kIntentMs }));
    $('btn-release').addEventListener('click', () => intent.set('btn-follow',
        { want: false, until: Date.now() + kIntentMs }));

    const live = (id, label, fmt, make) => {
        const r = $(id);
        r.addEventListener('input', () => {
            uiFrozenUntil = Date.now() + 700;
            $(label).textContent = fmt(+r.value);
            cmd(make(+r.value), true);
        });
    };
    live('r-yaw', 'v-yaw', (v) => `${v}°`, (v) => `sim imu ${v}`);
    live('r-vbat', 'v-vbat', (v) => `${v} mV`, (v) => `sim vbat ${v}`);
    live('r-vmax', 'v-vmax', (v) => `${v}`, (v) => `motion tune v_max ${v}`);
    live('r-vcoarse', 'v-vcoarse', (v) => `${v}`, (v) => `motion tune v_coarse ${v}`);
    live('r-backlash', 'v-backlash', (v) => `${v}`, (v) => `motion tune backlash ${v}`);
    live('r-cpm', 'v-cpm', (v) => `${v}`, (v) => `ui knob counts ${v}`);
    // The per-unit trim, in microsteps, shown in both units because one of them is what you
    // type on the bench and the other is what you can see through the glass.
    live('r-zeroh', 'v-zeroh', zeroLabel, (v) => `motion zero h ${v}`);
    live('r-zerom', 'v-zerom', zeroLabel, (v) => `motion zero m ${v}`);
    live('r-steps', 'v-steps', (v) => `${v}`, (v) => `chrono steps ${v}`);
    $('r-steps').addEventListener('input', () => {
        const n = +$('r-steps').value;
        $('steps-hint').textContent = n === 1
            ? 'one jump a minute — the hands are still in between, the way quartz ticks'
            : `one move every ${(60 / n).toFixed(1)} s of clock time`;
    });
    // Warp is logarithmic: 0.1x to 1000x reads naturally on a linear slider only in a log
    // scale, and the interesting settings (1x, 60x) are decades apart.
    const warpOf = (v) => Math.pow(10, (v - 25) / 25);
    live('r-warp', 'v-warp', (v) => `${warpOf(v).toFixed(2)}×`,
        (v) => `sim warp ${warpOf(v).toFixed(3)}`);

    $('cli-form').addEventListener('submit', (e) => {
        e.preventDefault();
        const line = $('cli').value.trim();
        if (!line) return;
        $('cli').value = '';
        cmd(line);
    });
}

// ---- boot ---------------------------------------------------------------------------------

(async function main() {
    G = await (await fetch('/geometry.json')).json();
    face = build($('plate'), G);
    wireKnob();
    wireHands();
    wireControls();
    connect();
    // Most of what the app does is gated behind `unsafe` (§9.6), and clicking "unsafe on"
    // by hand every 60 s is not a workflow.  The window slides on every unsafe command.
    setInterval(() => cmd('unsafe on', true), 30000);
    setTimeout(() => cmd('unsafe on', true), 300);
})();
