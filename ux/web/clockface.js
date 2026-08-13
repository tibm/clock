// The plate, drawn from cad/clock_plate.svg's own numbers.
//
// The SVG viewBox is millimetres with the origin at the dial centre, so every coordinate in
// here is the dimension you would measure on the aluminium.  Angles are the firmware's:
// 0 deg = 12 o'clock, clockwise positive, homing index at 0.

const SVG = 'http://www.w3.org/2000/svg';

export const polar = (r, deg) => {
    const a = (deg - 90) * Math.PI / 180;
    return [r * Math.cos(a), r * Math.sin(a)];
};

const el = (name, attrs = {}, parent = null) => {
    const n = document.createElementNS(SVG, name);
    for (const [k, v] of Object.entries(attrs)) n.setAttribute(k, v);
    if (parent) parent.appendChild(n);
    return n;
};

// A hand: a tapered blade with a short counterweight, drawn pointing at 0 deg and rotated
// into place.  Aluminium over walnut, so it is light on dark with a highlight down one edge.
function hand(g, cfg, cls) {
    const { length, width, tail } = cfg;
    const w = width / 2;
    const grp = el('g', { class: cls }, g);
    el('polygon', {
        points: [
            `0,${-length}`,
            `${w * 0.55},${-length + width * 1.6}`,
            `${w},${tail * 0.4}`,
            `${w * 0.8},${tail}`,
            `${-w * 0.8},${tail}`,
            `${-w},${tail * 0.4}`,
            `${-w * 0.55},${-length + width * 1.6}`,
        ].join(' '),
        class: 'hand-body',
    }, grp);
    el('line', { x1: 0, y1: -length + 1, x2: 0, y2: tail - 1, class: 'hand-spec' }, grp);
    return grp;
}

export function build(root, G) {
    const half = G.plate.size / 2;
    root.setAttribute('viewBox', `${-half - 4} ${-half - 4} ${G.plate.size + 8} ${G.plate.size + 8}`);

    const defs = el('defs', {}, root);

    const alu = el('linearGradient', { id: 'alu', x1: '0', y1: '0', x2: '0.4', y2: '1' }, defs);
    el('stop', { offset: '0', 'stop-color': '#cfd3d6' }, alu);
    el('stop', { offset: '0.45', 'stop-color': '#9aa1a6' }, alu);
    el('stop', { offset: '0.55', 'stop-color': '#b6bcc0' }, alu);
    el('stop', { offset: '1', 'stop-color': '#868d92' }, alu);

    const walnut = el('radialGradient', { id: 'walnut', cx: '0.42', cy: '0.36', r: '0.75' }, defs);
    el('stop', { offset: '0', 'stop-color': '#6b4a30' }, walnut);
    el('stop', { offset: '0.6', 'stop-color': '#4e3421' }, walnut);
    el('stop', { offset: '1', 'stop-color': '#33220f' }, walnut);

    const glassG = el('linearGradient', { id: 'glass', x1: '0', y1: '0', x2: '0.7', y2: '1' }, defs);
    el('stop', { offset: '0', 'stop-color': '#ffffff', 'stop-opacity': '0.16' }, glassG);
    el('stop', { offset: '0.45', 'stop-color': '#ffffff', 'stop-opacity': '0.02' }, glassG);
    el('stop', { offset: '1', 'stop-color': '#ffffff', 'stop-opacity': '0.07' }, glassG);

    const soft = el('filter', { id: 'soft', x: '-60%', y: '-60%', width: '220%', height: '220%' }, defs);
    el('feGaussianBlur', { stdDeviation: '3.2' }, soft);
    const softer = el('filter', { id: 'softer', x: '-90%', y: '-90%', width: '280%', height: '280%' }, defs);
    el('feGaussianBlur', { stdDeviation: '7' }, softer);
    const drop = el('filter', { id: 'drop', x: '-30%', y: '-30%', width: '160%', height: '160%' }, defs);
    el('feDropShadow', { dx: '0.5', dy: '1.4', stdDeviation: '0.9', 'flood-opacity': '0.55' }, drop);

    el('clipPath', { id: 'dialClip' }, defs)
        .appendChild(el('circle', { cx: 0, cy: 0, r: G.dial.opening_d / 2 }));

    // Everything physical hangs off ONE group so the whole cube can be turned about the dial
    // centre -- which is where the IMU's yaw goes.  The origin is the centre of rotation
    // already, so the transform is a bare rotate().
    const rot = el('g', { id: 'plate-rot' }, root);

    // ---- the wake COB, which is on the BACK: seen only as a wash around the cube --------
    const wake = el('rect', {
        x: -half - 3, y: -half - 3, width: G.plate.size + 6, height: G.plate.size + 6,
        rx: 8, id: 'wake-glow', filter: 'url(#softer)', fill: '#000', opacity: '0',
    }, rot);
    wake.dataset.role = 'wake';

    // ---- the plate ---------------------------------------------------------------------
    el('rect', {
        x: -half, y: -half, width: G.plate.size, height: G.plate.size, rx: 1.2,
        fill: 'url(#alu)', stroke: '#5c6266', 'stroke-width': 0.3,
    }, rot);

    const inset = half - G.plate.corner_screw_inset;
    for (const [sx, sy] of [[-1, -1], [1, -1], [-1, 1], [1, 1]]) {
        el('circle', {
            cx: sx * inset, cy: sy * inset, r: G.plate.corner_screw_d / 2,
            fill: '#5b6165', stroke: '#d5d9db', 'stroke-width': 0.18,
        }, rot);
    }

    // ---- the dial ----------------------------------------------------------------------
    const dial = el('g', { 'clip-path': 'url(#dialClip)' }, rot);
    el('circle', { cx: 0, cy: 0, r: G.dial.opening_d / 2, fill: 'url(#walnut)' }, dial);

    // The two on-PCB pixels wash the walnut from behind the glass (chain positions 1-2).
    for (const [i, idx] of G.dial_pixels.pixel_index.entries()) {
        const [x, y] = polar(G.dial_pixels.r, G.dial_pixels.angle[i]);
        const w = el('circle', {
            cx: x, cy: y, r: 26, filter: 'url(#softer)', fill: '#000', opacity: '0',
        }, dial);
        w.dataset.role = 'dialwash';
        w.dataset.px = idx;
    }

    // 12 batons.  The CAD has rectangles, not the dots README §3 describes.
    for (let i = 0; i < G.ticks.count; i++) {
        el('rect', {
            x: -G.ticks.width / 2, y: -G.ticks.r_outer,
            width: G.ticks.width, height: G.ticks.r_outer - G.ticks.r_inner,
            transform: `rotate(${i * 30})`, fill: '#15100a', opacity: 0.92,
        }, dial);
    }

    // The homing window: a hole in the dial at r = 23.5 mm, due north.  It glows when the
    // QRE1113 is seeing an index mark, which is the whole story of a homing sweep.
    const hw = el('rect', {
        x: -G.homing.window_w / 2, y: -G.homing.r - G.homing.window_h / 2,
        width: G.homing.window_w, height: G.homing.window_h, rx: 0.4,
        fill: '#0b0906', stroke: '#000', 'stroke-width': 0.15,
    }, dial);
    const optoGlow = el('circle', {
        cx: 0, cy: -G.homing.r, r: 4.5, fill: '#ff5a2b', opacity: 0, filter: 'url(#soft)',
    }, dial);
    optoGlow.dataset.role = 'opto';
    hw.dataset.role = 'window';

    // ---- hands -------------------------------------------------------------------------
    const handG = el('g', { filter: 'url(#drop)' }, dial);
    const hourG = hand(handG, G.hands.hour, 'hand hour');
    const minG = hand(handG, G.hands.minute, 'hand minute');
    hourG.dataset.hand = 'h';
    minG.dataset.hand = 'm';

    el('circle', { cx: 0, cy: 0, r: G.dial.hub_d / 2 + 0.9, fill: '#cdd2d5' }, dial);
    el('circle', { cx: 0, cy: 0, r: G.dial.hub_d / 2, fill: '#3a3f43' }, dial);

    // glass
    el('circle', {
        cx: 0, cy: 0, r: G.dial.opening_d / 2, fill: 'url(#glass)',
        stroke: '#6d7478', 'stroke-width': 0.5, 'pointer-events': 'none',
    }, rot);

    // ---- the five status pixels, behind their face holes --------------------------------
    const S = G.status_leds;
    const x0 = -((S.count - 1) * S.pitch) / 2;
    const leds = [];
    for (let i = 0; i < S.count; i++) {
        const cx = x0 + i * S.pitch;
        const g = el('g', {}, rot);
        const glow = el('circle', {
            cx, cy: S.y, r: 5.2, fill: '#000', opacity: 0, filter: 'url(#soft)',
        }, g);
        el('circle', {
            cx, cy: S.y, r: S.hole_d / 2, fill: '#2b3033',
            stroke: '#767d81', 'stroke-width': 0.2,
        }, g);
        const lens = el('circle', { cx, cy: S.y, r: S.hole_d / 2 - 0.35, fill: '#000' }, g);
        el('text', {
            x: cx, y: S.y + 6.2, class: 'icon-label', 'text-anchor': 'middle',
        }, g).textContent = S.labels[i];
        leds.push({ glow, lens, px: S.pixel_index[i] });
    }

    return {
        rot,
        hour: hourG,
        minute: minG,
        leds,
        wake,
        optoGlow,
        dialWash: [...root.querySelectorAll('[data-role="dialwash"]')],
    };
}
