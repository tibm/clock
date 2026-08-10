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

// Fire-and-forget with a reply: `res` comes back tagged so we can print it next to the
// command that caused it.
function cmd(line, quiet = false) {
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

function onState(s) {
    face.hour.setAttribute('transform', `rotate(${s.hands.h})`);
    face.minute.setAttribute('transform', `rotate(${s.hands.m})`);

    const two = (n) => String(n).padStart(2, '0');
    $('pill-clock').textContent = s.clock.valid
        ? `${two(s.clock.h)}:${two(s.clock.m)}:${two(s.clock.s)}${s.clock.follow ? '' : ' ⏸'}`
        : '--:--:--';
    $('pill-sim').textContent = `sim ${(s.ms / 1000).toFixed(1)} s`;
    $('pill-warp').textContent = `warp ${s.warp.toFixed(2)}×`;
    // The FSM's own word for what it is doing, not this app's guess at it.
    $('pill-motion').textContent = s.motion.phase
        ? `${s.motion.state} · ${s.motion.phase}`
        : `${s.motion.state}${s.motion.homed ? ' · homed' : ''}`;
    $('pill-motion').classList.toggle('up', s.motion.homed && s.motion.state !== 'fault');
    $('pill-mode').textContent = s.ui.mode
        + (s.ui.idle_in ? ` · ${(s.ui.idle_in / 1000).toFixed(1)}s` : '');
    $('c-alarm').textContent = `${two(s.ui.alarm_h)}:${two(s.ui.alarm_m)} ${s.ui.armed ? 'armed' : 'off'}`;
    $('c-vol').textContent = `${s.ui.vol}%`;

    $('m-h').textContent = `${s.hands.h.toFixed(1)}°`;
    $('m-m').textContent = `${s.hands.m.toFixed(1)}°`;
    $('m-pos').textContent = `${s.hands.hp} / ${s.hands.mp}`;
    $('m-vel').textContent = `${s.hands.hv} / ${s.hands.mv}`;

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

    $('t-radio').classList.toggle('on', s.radio_off);
    $('t-radio').textContent = s.radio_off ? 'radio OFF' : 'radio off';
    $('t-plug').classList.toggle('on', s.pwr.plugged);
    $('t-plug').textContent = s.pwr.plugged ? `plugged · ${s.pwr.soc}%` : `battery · ${s.pwr.soc}%`;

    if (Date.now() > uiFrozenUntil) {
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
        const d = Math.sign(e.deltaY) * 4;      // one detent per notch
        visual += d * 360 / 256;
        $('knob-dial').setAttribute('transform', `rotate(${visual})`);
        knobBy(d);
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
        if (e.target.tagName === 'INPUT') return;
        if (e.key === 'ArrowRight') knobBy(4);
        else if (e.key === 'ArrowLeft') knobBy(-4);
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
        cmd(`sim hand ${dragging} ${angleAt(svg, e).toFixed(1)}`, true);
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
    $('t-radio').addEventListener('click', (e) =>
        cmd(`sim radio ${e.currentTarget.classList.contains('on') ? 'off' : 'on'}`));
    $('t-plug').addEventListener('click', (e) =>
        cmd(e.currentTarget.classList.contains('on') ? 'sim unplug' : 'sim plug'));

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
