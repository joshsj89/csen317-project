/*
 * Chord ring animation for the DHT showcase.
 *
 * Node and key IDs use the same hash as the C++ implementation:
 * libstdc++'s std::hash<std::string> (_Hash_bytes, 64-bit, seed 0xc70f6907),
 * then mod 2^m with m = 8. The port below was checked against g++ output,
 * so "node-A" really is 143 and "file1.txt" really is 170.
 *
 * Routing mirrors chord.cpp: find_successor() answers when the ID falls in
 * (n, successor], otherwise forwards to closest_preceding_finger(), scanning
 * fingers from m-1 down to 0.
 */
(() => {
    'use strict';

    // ---------- std::hash<std::string> (libstdc++, 64-bit) ----------
    const MASK = (1n << 64n) - 1n;
    const MUL = 0xc6a4a7935bd1e995n;
    const SEED = 0xc70f6907n;
    const shiftMix = v => v ^ (v >> 47n);

    function stdHash(str) {
        const b = new TextEncoder().encode(str);
        const len = b.length;
        const aligned = len & ~7;
        let h = (SEED ^ (BigInt(len) * MUL)) & MASK;
        for (let i = 0; i < aligned; i += 8) {
            let d = 0n;
            for (let j = 7; j >= 0; j--) d = (d << 8n) | BigInt(b[i + j]);
            d = (shiftMix((d * MUL) & MASK) * MUL) & MASK;
            h ^= d;
            h = (h * MUL) & MASK;
        }
        if (len & 7) {
            let d = 0n;
            for (let j = len - 1; j >= aligned; j--) d = (d << 8n) | BigInt(b[j]);
            h ^= d;
            h = (h * MUL) & MASK;
        }
        h = (shiftMix(h) * MUL) & MASK;
        return shiftMix(h);
    }

    const M = 8;
    const RING = 1 << M;
    const R_FACTOR = 3;
    const ringId = s => Number(stdHash(s) % BigInt(RING));

    // ---------- Ring math (same intervals as utils.cpp) ----------
    function inInclusive(start, end, id) { // (start, end]
        if (start < end) return id > start && id <= end;
        if (start > end) return id > start || id <= end;
        return true;
    }
    function inOpen(start, end, id) { // (start, end)
        if (start < end) return id > start && id < end;
        if (start > end) return id > start || id < end;
        return id !== start;
    }

    function liveIds(state) {
        return state.nodes.filter(n => n.alive).map(n => n.id).sort((a, b) => a - b);
    }
    function successorOf(state, id) { // first live node with nodeId >= id
        const ids = liveIds(state);
        if (!ids.length) return null;
        return ids.find(n => n >= id) ?? ids[0];
    }
    function nextNode(state, id) { // first live node strictly after id
        const ids = liveIds(state);
        return ids.find(n => n > id) ?? ids[0];
    }
    function prevNode(state, id) {
        const ids = liveIds(state);
        const below = ids.filter(n => n < id);
        return below.length ? below[below.length - 1] : ids[ids.length - 1];
    }
    function fingers(state, id) {
        const out = [];
        for (let k = 0; k < M; k++) {
            const start = (id + (1 << k)) % RING;
            out.push({ k, start, node: successorOf(state, start) });
        }
        return out;
    }
    // Returns the forwarding path and the owner, as chord.cpp would compute it.
    function route(state, from, keyId) {
        const path = [from];
        let cur = from;
        for (let guard = 0; guard < 32; guard++) {
            const succ = nextNode(state, cur);
            if (inInclusive(cur, succ, keyId)) return { path, owner: succ };
            const f = fingers(state, cur);
            let next = cur;
            for (let k = M - 1; k >= 0; k--) {
                if (inOpen(cur, keyId, f[k].node)) { next = f[k].node; break; }
            }
            if (next === cur) return { path, owner: succ };
            path.push(next);
            cur = next;
        }
        return { path, owner: successorOf(state, keyId) };
    }
    function replicaTargets(state, owner) {
        const out = [];
        let cur = owner;
        while (out.length < R_FACTOR) {
            cur = nextNode(state, cur);
            if (cur === owner || out.includes(cur)) break;
            out.push(cur);
        }
        return out;
    }

    // ---------- DOM helpers ----------
    const SVGNS = 'http://www.w3.org/2000/svg';
    const el = (tag, attrs = {}, parent) => {
        const e = document.createElementNS(SVGNS, tag);
        for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
        if (parent) parent.appendChild(e);
        return e;
    };
    const reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    const sleep = ms => new Promise(r => setTimeout(r, reduceMotion ? Math.min(ms, 60) : ms));

    const CX = 260, CY = 260, RR = 190;
    const angle = id => (id / RING) * Math.PI * 2 - Math.PI / 2;
    const pos = (id, r = RR) => [CX + r * Math.cos(angle(id)), CY + r * Math.sin(angle(id))];

    // ---------- Simulation view ----------
    class RingView {
        constructor(root) {
            this.root = root;
            this.svg = root.querySelector('svg.ring');
            this.logEl = root.querySelector('.sim-log');
            this.tableEl = root.querySelector('.sim-table');
            this.fingerEl = root.querySelector('.sim-fingers');
            this.token = 0; // cancels running animations when the scene changes
            this.build();
        }

        build() {
            const s = this.svg;
            s.innerHTML = '';
            el('circle', { cx: CX, cy: CY, r: RR, class: 'ring-track' }, s);
            for (let i = 0; i < RING; i += 16) {
                const [x1, y1] = pos(i, RR - 4), [x2, y2] = pos(i, RR + 4);
                el('line', { x1, y1, x2, y2, class: i % 64 === 0 ? 'tick major' : 'tick' }, s);
                if (i % 64 === 0) {
                    const [tx, ty] = pos(i, RR - 18);
                    el('text', { x: tx, y: ty + 4, class: 'tick-label' }, s).textContent = i;
                }
            }
            this.gArcs = el('g', {}, s);
            this.gFingers = el('g', {}, s);
            this.gKeys = el('g', {}, s);
            this.gNodes = el('g', {}, s);
            this.gPackets = el('g', {}, s);
            this.center = el('text', { x: CX, y: CY - 6, class: 'center-big' }, s);
            this.centerSub = el('text', { x: CX, y: CY + 16, class: 'center-sub' }, s);
            this.setCenter('m = 8', '2⁸ = 256 identifiers');
        }

        setCenter(a, b) { this.center.textContent = a; this.centerSub.textContent = b; }

        colorOf(state, id) {
            const idx = state.nodes.findIndex(n => n.id === id);
            return `var(--n${(state.nodes[idx]?.color ?? 0) % 8})`;
        }

        // Draw the static state: arcs, keys, nodes, tables.
        render(state, opts = {}) {
            this.state = state;
            this.gArcs.innerHTML = '';
            this.gKeys.innerHTML = '';
            this.gNodes.innerHTML = '';
            if (!opts.keepFingers) this.gFingers.innerHTML = '';

            const ids = liveIds(state);
            if (opts.arcs && ids.length) {
                for (const id of ids) {
                    const p = ids.length === 1 ? (id + 1) % RING : prevNode(state, id);
                    this.arc(p, id, this.colorOf(state, id));
                }
            }

            for (const k of state.keys) {
                const owner = k.owner ?? successorOf(state, k.id);
                const [x, y] = pos(k.id, RR - 30);
                const g = el('g', { class: 'key', transform: `translate(${x},${y})` }, this.gKeys);
                el('rect', { x: -5, y: -5, width: 10, height: 10, rx: 2, transform: 'rotate(45)', fill: owner != null ? this.colorOf(state, owner) : 'var(--muted)' }, g);
                const [lx, ly] = pos(k.id, RR - 50);
                const t = el('text', { x: lx - x, y: ly - y + 4, class: 'key-label' }, g);
                t.textContent = `${k.name} · ${k.id}`;
            }

            // Nodes closer than ~9 IDs would overlap; push every other one outward.
            this.layout = new Map();
            const sorted = [...state.nodes].sort((a, b) => a.id - b.id);
            let prevId = null, prevOut = false;
            for (const n of sorted) {
                const close = prevId != null && n.id - prevId < 10;
                const out = close && !prevOut;
                this.layout.set(n.id, out ? RR + 40 : RR);
                prevId = n.id; prevOut = out;
            }
            for (const n of state.nodes) {
                const nr = this.layout.get(n.id);
                if (nr !== RR) {
                    const [ax, ay] = pos(n.id, RR), [bx, by] = pos(n.id, nr - 17);
                    el('line', { x1: ax, y1: ay, x2: bx, y2: by, class: 'leader' }, this.gNodes);
                    el('circle', { cx: ax, cy: ay, r: 3, class: 'leader-dot' }, this.gNodes);
                }
            }
            for (const n of state.nodes) {
                const nr = this.layout.get(n.id);
                const [x, y] = pos(n.id, nr);
                const g = el('g', { class: 'node' + (n.alive ? '' : ' dead') + (opts.focus === n.id ? ' focus' : ''), transform: `translate(${x},${y})`, 'data-id': n.id }, this.gNodes);
                el('circle', { r: 17, fill: n.alive ? this.colorOf(state, n.id) : 'var(--surface)' }, g);
                el('text', { y: 4.5, class: 'node-id' }, g).textContent = n.id;
                const [lx, ly] = pos(n.id, nr + 34);
                const lbl = el('text', { x: lx - x, y: ly - y + 4, class: 'node-name' }, g);
                lbl.textContent = n.name;
                if (!n.alive) {
                    el('path', { d: 'M-9,-9 L9,9 M9,-9 L-9,9', class: 'dead-x' }, g);
                }
                g.addEventListener('click', () => this.onNodeClick?.(n.id));
            }

            this.renderTable(state, opts);
            if (opts.fingersOf != null) this.renderFingers(state, opts.fingersOf, opts.fingerHighlight);
            else if (this.fingerEl) this.fingerEl.hidden = true;
        }

        arc(from, to, color) {
            // clockwise arc covering (from, to]
            let span = (to - from + RING) % RING || RING;
            const a0 = angle(from), a1 = a0 + (span / RING) * Math.PI * 2;
            const r = RR;
            const [x0, y0] = [CX + r * Math.cos(a0), CY + r * Math.sin(a0)];
            const [x1, y1] = [CX + r * Math.cos(a1), CY + r * Math.sin(a1)];
            const large = span > RING / 2 ? 1 : 0;
            el('path', { d: `M${x0},${y0} A${r},${r} 0 ${large} 1 ${x1},${y1}`, class: 'own-arc', stroke: color }, this.gArcs);
        }

        renderTable(state, opts) {
            if (!this.tableEl) return;
            const rows = liveIds(state).map(id => {
                const n = state.nodes.find(x => x.id === id);
                const prim = state.keys.filter(k => (k.owner ?? successorOf(state, k.id)) === id).map(k => k.name);
                const reps = state.keys.filter(k => (k.replicas || []).includes(id)).map(k => k.name);
                return `<tr${opts.focus === id ? ' class="hl"' : ''}><th><span class="dot" style="background:${this.colorOf(state, id)}"></span>${n.name} <span class="muted">${id}</span></th><td>${prim.join(', ') || '<span class="muted">-</span>'}</td>${opts.replicas ? `<td>${reps.join(', ') || '<span class="muted">-</span>'}</td>` : ''}</tr>`;
            }).join('');
            const dead = state.nodes.filter(n => !n.alive).map(n => `<tr class="dead-row"><th>${n.name} <span class="muted">${n.id}</span></th><td colspan="2">crashed</td></tr>`).join('');
            this.tableEl.innerHTML = `<table><thead><tr><th>Node</th><th>Primary keys</th>${opts.replicas ? '<th>Replicas</th>' : ''}</tr></thead><tbody>${rows}${dead}</tbody></table>`;
        }

        renderFingers(state, id, highlight) {
            if (!this.fingerEl) return;
            const f = fingers(state, id);
            this.fingerEl.hidden = false;
            this.fingerEl.innerHTML = `<p class="ft-title">Finger table of node ${id}</p><table><thead><tr><th>i</th><th>start = ${id} + 2<sup>i</sup></th><th>successor</th></tr></thead><tbody>${f.map(r => `<tr${highlight === r.k ? ' class="hl"' : ''}><td>${r.k}</td><td>${r.start}</td><td>${r.node}</td></tr>`).join('')}</tbody></table>`;
            this.gFingers.innerHTML = '';
            const [x0, y0] = this.nodePos(id);
            const seen = new Set();
            for (const r of f) {
                if (r.node === id || seen.has(r.node)) continue;
                seen.add(r.node);
                const [x1, y1] = this.nodePos(r.node);
                el('path', { d: `M${x0},${y0} Q${CX},${CY} ${x1},${y1}`, class: 'finger' }, this.gFingers);
            }
        }

        nodePos(id) {
            const r = this.layout?.get(id) ?? RR;
            return pos(id, r === RR ? RR - 20 : r - 20);
        }

        log(html, cls = '') {
            if (!this.logEl) return;
            const li = document.createElement('li');
            li.innerHTML = html;
            if (cls) li.className = cls;
            this.logEl.appendChild(li);
            this.logEl.scrollTop = this.logEl.scrollHeight;
        }
        clearLog() { if (this.logEl) this.logEl.innerHTML = ''; }

        // Animate a message between two ring positions.
        async packet(from, to, label, kind = 'req', token = this.token) {
            if (token !== this.token) throw new Error('cancelled');
            const [x0, y0] = this.nodePos(from), [x1, y1] = this.nodePos(to);
            const mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
            const qx = mx + (CX - mx) * 0.55, qy = my + (CY - my) * 0.55;
            const path = el('path', { d: `M${x0},${y0} Q${qx},${qy} ${x1},${y1}`, class: `trail ${kind}` }, this.gPackets);
            const len = path.getTotalLength();
            path.style.strokeDasharray = len;
            path.style.strokeDashoffset = len;
            const dot = el('circle', { r: 6, class: `packet ${kind}` }, this.gPackets);
            const tag = el('text', { class: `packet-label ${kind}` }, this.gPackets);
            tag.textContent = label;
            const dur = reduceMotion ? 1 : 1100;
            const t0 = performance.now();
            await new Promise(resolve => {
                const step = now => {
                    if (token !== this.token) { resolve(); return; }
                    const t = Math.min(1, (now - t0) / dur);
                    const e = t < .5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2;
                    const p = path.getPointAtLength(len * e);
                    dot.setAttribute('cx', p.x); dot.setAttribute('cy', p.y);
                    tag.setAttribute('x', p.x); tag.setAttribute('y', p.y - 12);
                    path.style.strokeDashoffset = len * (1 - e);
                    if (t < 1) requestAnimationFrame(step); else resolve();
                };
                requestAnimationFrame(step);
            });
            if (token !== this.token) throw new Error('cancelled');
            dot.remove(); tag.remove();
            path.classList.add('done');
            return path;
        }
        clearPackets() { this.gPackets.innerHTML = ''; }

        pulse(id, cls = 'pulse') {
            const g = this.gNodes.querySelector(`[data-id="${id}"]`);
            if (!g) return;
            g.classList.remove(cls); void g.getBBox(); g.classList.add(cls);
        }
    }

    // ---------- Guided tour ----------
    const BASE_NODES = [
        { name: 'node-A', color: 0 }, { name: 'node-B', color: 1 },
        { name: 'node-C', color: 2 }, { name: 'node-D', color: 3 },
    ];
    const FILES = [
        { name: 'file1.txt', value: 'contents-1' }, { name: 'image.png', value: 'png-bytes' },
        { name: 'video.mp4', value: 'video-bytes' }, { name: 'notes.md', value: 'some notes' },
    ];
    const mkNodes = list => list.map(n => ({ ...n, id: ringId(n.name), alive: true }));
    const mkKeys = () => FILES.map(f => ({ ...f, id: ringId(f.name) }));
    const fullState = () => ({ nodes: mkNodes(BASE_NODES), keys: mkKeys() });
    const withE = () => ({ nodes: mkNodes([...BASE_NODES, { name: 'node-E', color: 4 }]), keys: mkKeys() });
    const withReplicas = st => {
        for (const k of st.keys) { const o = successorOf(st, k.id); k.owner = o; k.replicas = replicaTargets(st, o); }
        return st;
    };
    const code = s => `<code>${s}</code>`;

    const SCENES = [
        {
            title: 'One ring of identifiers',
            text: `Chord puts every node and every key on the same circle of IDs. We ran with ${code('m = 8')} bits, so IDs go from 0 to 255 and wrap around. Real deployments use 160 bits. Eight bits keep the ring small enough to check by hand.`,
            async play(v) {
                v.render({ nodes: [], keys: [] });
                v.setCenter('m = 8', '2⁸ = 256 identifiers');
            },
        },
        {
            title: 'Nodes hash onto the ring',
            text: `A node's ID is ${code('std::hash(ip:port) mod 2^m')}. The names below go through the same hash function as our C++ code. A node owns every ID from just after its predecessor up to its own ID.`,
            async play(v, tok) {
                const st = { nodes: [], keys: [] };
                const all = fullState().nodes;
                v.render(st);
                for (const n of all) {
                    st.nodes.push(n);
                    v.render(st, { arcs: true });
                    v.pulse(n.id);
                    v.log(`hash("${n.name}") mod 256 = <b>${n.id}</b>`);
                    await sleep(650);
                    if (tok !== v.token) return;
                }
                v.setCenter('4 nodes', '84 · 102 · 107 · 143');
            },
        },
        {
            title: 'Keys hash onto the same ring',
            text: `A file's key is hashed the same way and stored on its <em>successor</em>, the first node clockwise from the key's ID. Here ${code('notes.md')} (38), ${code('file1.txt')} (170) and ${code('video.mp4')} (178) all wrap around to node 84.`,
            async play(v, tok) {
                const st = { nodes: fullState().nodes, keys: [] };
                for (const k of mkKeys()) {
                    st.keys.push(k);
                    v.render(st, { arcs: true });
                    v.log(`hash("${k.name}") mod 256 = <b>${k.id}</b> → stored on node ${successorOf(st, k.id)}`);
                    await sleep(650);
                    if (tok !== v.token) return;
                }
                v.setCenter('4 keys', 'each on its successor');
            },
        },
        {
            title: 'Finger tables',
            text: `Each node keeps ${code('m')} fingers. Finger <em>i</em> points to the successor of ${code('n + 2^i')}, so the fingers reach ¼, ½ and the far side of the ring. That lets a lookup cut the remaining distance roughly in half with each hop, which gives O(log N) hops.`,
            async play(v) {
                const st = fullState();
                v.render(st, { arcs: true, focus: 107, fingersOf: 107 });
                v.setCenter('node 107', '8 fingers, 2 distinct');
                v.log('Fingers 0–5 (starts 108…139) → node 143');
                v.log('Fingers 6–7 (starts 171, 235) wrap → node 84');
            },
        },
        {
            title: 'Looking up file1.txt from node 107',
            text: `170 is not in (107, 143], so node 107 forwards to its <em>closest preceding finger</em>, node 143. Node 143 sees that 170 is in (143, 84] and replies that node 84 owns it. Node 107 then sends ${code('GET')} straight to 84.`,
            async play(v, tok) {
                const st = fullState();
                v.render(st, { arcs: true, focus: 107, fingersOf: 107, fingerHighlight: 5 });
                v.setCenter('GET file1.txt', 'key ID 170');
                await sleep(500);
                v.log(`107: 170 ∉ (107, 143] → closest preceding finger = <b>143</b>`);
                await v.packet(107, 143, 'FIND_SUCCESSOR 170', 'req', tok);
                v.render(st, { arcs: true, focus: 143, fingersOf: 143, keepFingers: false });
                v.log(`143: 170 ∈ (143, 84] → owner is <b>84</b>`);
                await v.packet(143, 107, 'OK 84', 'resp', tok);
                await v.packet(107, 84, 'GET file1.txt', 'req', tok);
                v.pulse(84);
                await v.packet(84, 107, 'OK contents-1', 'resp', tok);
                v.log('84 → 107: <b>contents-1</b>. Two routing messages, then one GET.', 'ok');
                v.setCenter('found on 84', '1 forward + 1 GET');
            },
        },
        {
            title: 'node-E joins and takes over keys',
            text: `node-E hashes to 78. It asks any existing node (here 143) for ${code('FIND_SUCCESSOR 78')}, gets 84, and sends ${code('TRANSFER_KEYS')}. Node 84 hands over every key in (143, 78], then a ${code('NOTIFY')} and the next ${code('stabilize()')} round fix both neighbours' pointers.`,
            async play(v, tok) {
                const before = fullState();
                const after = withE();
                // Show node-E outside the ring first
                v.render(before, { arcs: true });
                v.setCenter('node-E', 'hash → 78');
                const ghost = { nodes: [...before.nodes, { ...after.nodes[4], alive: true }], keys: before.keys.map(k => ({ ...k, owner: successorOf(before, k.id) })) };
                v.render(ghost, { arcs: false, focus: 78 });
                v.log('hash("node-E") mod 256 = <b>78</b>');
                await sleep(400);
                await v.packet(78, 143, 'FIND_SUCCESSOR 78', 'req', tok);
                v.log('143: 78 ∈ (143, 84] → successor is <b>84</b>');
                await v.packet(143, 78, 'OK 84', 'resp', tok);
                await v.packet(78, 84, 'TRANSFER_KEYS 78', 'req', tok);
                v.log('84 moves keys in (143, 78]: notes.md, file1.txt, video.mp4');
                await v.packet(84, 78, 'PUT_PRIMARY ×3', 'resp', tok);
                v.render(after, { arcs: true, focus: 78 });
                v.pulse(78);
                await v.packet(78, 84, 'NOTIFY 78', 'req', tok);
                v.log('84: predecessor 143 → <b>78</b>');
                await v.packet(143, 84, 'GET_PREDECESSOR', 'req', tok);
                await v.packet(84, 143, 'OK 78', 'resp', tok);
                v.log('143 stabilize(): successor 84 → <b>78</b>', 'ok');
                v.setCenter('5 nodes', 'only 84’s keys moved');
            },
        },
        {
            title: 'Replication on successors (r = 3)',
            text: `After a ${code('PUT')}, the owner stores the primary copy and pushes ${code('PUT_REPLICA')} to the next ${code('r = 3')} nodes on its successor list. Copies on neighbouring nodes are already in the right place if the owner fails. Writes are eventually consistent: the owner updates first and the replicas follow.`,
            async play(v, tok) {
                const st = withReplicas(withE());
                const blank = withE();
                v.render(blank, { arcs: true, replicas: true });
                v.setCenter('r = 3', 'primary + 3 successors');
                for (const [owner, keyName] of [[78, 'file1.txt'], [102, 'image.png']]) {
                    const targets = replicaTargets(st, owner);
                    for (const t of targets) await v.packet(owner, t, `PUT_REPLICA ${keyName}`, 'rep', tok);
                    v.log(`${owner} → ${targets.join(', ')}: replicas of ${keyName}`);
                }
                v.render(st, { arcs: true, replicas: true });
                v.log('Every key now lives on 4 of the 5 nodes.', 'ok');
            },
        },
        {
            title: 'A node crashes',
            text: `node-E (78) is killed. Node 143's ${code('PING')} times out after 1 second, and ${code('check_successor()')} plus a few stabilize rounds reconnect 143 to 84. Every 5 seconds ${code('repair_primary_status()')} runs, and 84 promotes its replicas to primaries. The lookup for ${code('file1.txt')} still succeeds.`,
            async play(v, tok) {
                const st = withReplicas(withE());
                v.render(st, { arcs: true, replicas: true });
                await sleep(500);
                const crashed = withE();
                crashed.nodes.find(n => n.id === 78).alive = false;
                // keys keep their old placement until repair runs
                crashed.keys = st.keys.map(k => ({ ...k }));
                v.render(crashed, { arcs: false, replicas: true });
                v.log('node-E (78) crashed', 'err');
                v.setCenter('78 is down', 'detecting…');
                await v.packet(143, 78, 'PING', 'req', tok);
                v.log('143 → 78: PING … no reply after 1 s (timeout)', 'err');
                await sleep(300);
                await v.packet(143, 84, 'GET_PREDECESSOR', 'req', tok);
                v.log('143: successor → <b>84</b> (check_successor + stabilize)');
                const repaired = withReplicas((() => { const s = withE(); s.nodes.find(n => n.id === 78).alive = false; return s; })());
                v.render(repaired, { arcs: true, replicas: true, focus: 84 });
                v.pulse(84);
                v.log('84: repair_primary_status() → now primary for notes.md, file1.txt, video.mp4');
                await sleep(400);
                await v.packet(107, 143, 'FIND_SUCCESSOR 170', 'req', tok);
                await v.packet(143, 107, 'OK 84', 'resp', tok);
                await v.packet(107, 84, 'GET file1.txt', 'req', tok);
                await v.packet(84, 107, 'OK contents-1', 'resp', tok);
                v.log('GET file1.txt still returns <b>contents-1</b>', 'ok');
                v.setCenter('no data lost', 'replicas took over');
            },
        },
    ];

    function initTour(root) {
        const v = new RingView(root);
        const titleEl = root.querySelector('.scene-title');
        const textEl = root.querySelector('.scene-text');
        const stepEl = root.querySelector('.scene-step');
        const dots = root.querySelector('.scene-dots');
        const prev = root.querySelector('[data-act="prev"]');
        const next = root.querySelector('[data-act="next"]');
        const replay = root.querySelector('[data-act="replay"]');
        let i = 0;

        SCENES.forEach((s, k) => {
            const b = document.createElement('button');
            b.type = 'button';
            b.setAttribute('aria-label', `Step ${k + 1}: ${s.title}`);
            b.addEventListener('click', () => go(k));
            dots.appendChild(b);
        });

        let started = false;
        async function go(k) {
            started = true;
            i = Math.max(0, Math.min(SCENES.length - 1, k));
            const tok = ++v.token;
            const s = SCENES[i];
            titleEl.textContent = s.title;
            textEl.innerHTML = s.text;
            stepEl.textContent = `${i + 1} / ${SCENES.length}`;
            [...dots.children].forEach((d, j) => d.setAttribute('aria-current', j === i ? 'step' : 'false'));
            prev.disabled = i === 0;
            next.textContent = i === SCENES.length - 1 ? 'Try it yourself ↓' : 'Next →';
            v.clearLog();
            v.clearPackets();
            try { await s.play(v, tok); } catch (e) { if (e.message !== 'cancelled') console.error(e); }
        }

        prev.addEventListener('click', () => go(i - 1));
        next.addEventListener('click', () => {
            if (i === SCENES.length - 1) document.getElementById('sandbox')?.scrollIntoView({ behavior: reduceMotion ? 'auto' : 'smooth' });
            else go(i + 1);
        });
        replay.addEventListener('click', () => go(i));

        // Start when scrolled into view so the first animation isn't missed
        const io = new IntersectionObserver(es => {
            if (!started && es.some(e => e.isIntersecting)) { started = true; go(0); io.disconnect(); }
        }, { threshold: .25 });
        io.observe(root);
    }

    // ---------- Sandbox ----------
    function initSandbox(root) {
        const v = new RingView(root);
        const st = { nodes: mkNodes(BASE_NODES), keys: mkKeys() };
        let colorNext = 4;
        let focus = null;
        const keyInput = root.querySelector('#sb-key');
        const fromSel = root.querySelector('#sb-from');
        const nodeInput = root.querySelector('#sb-node');
        const hashOut = root.querySelector('.sb-hash');
        const status = root.querySelector('.sb-status');
        let busy = false;

        const draw = () => {
            v.render(st, { arcs: true, focus, fingersOf: focus ?? undefined });
            const live = liveIds(st);
            const cur = fromSel.value;
            fromSel.innerHTML = live.map(id => `<option value="${id}">${st.nodes.find(n => n.id === id).name} (${id})</option>`).join('');
            if (live.includes(+cur)) fromSel.value = cur;
            v.setCenter(`${live.length} node${live.length === 1 ? '' : 's'}`, `${st.keys.length} key${st.keys.length === 1 ? '' : 's'}`);
        };
        const say = (msg, cls = '') => { status.textContent = msg; status.className = 'sb-status ' + cls; };

        v.onNodeClick = id => {
            if (busy) return;
            focus = focus === id ? null : id;
            draw();
        };

        keyInput.addEventListener('input', () => {
            const s = keyInput.value.trim();
            hashOut.innerHTML = s ? `hash("${escapeHtml(s)}") mod 256 = <b>${ringId(s)}</b>` : '&nbsp;';
        });

        root.querySelector('[data-act="put"]').addEventListener('click', async () => {
            const name = keyInput.value.trim();
            if (!name || busy) return;
            const id = ringId(name);
            const from = +fromSel.value;
            await run(async tok => {
                const { path, owner } = route(st, from, id);
                await animateRoute(path, owner, id, tok);
                if (owner !== from) await v.packet(from, owner, `PUT ${name}`, 'req', tok);
                else v.log(`node ${from} is the owner: store locally`);
                const existing = st.keys.find(k => k.name === name);
                if (!existing) st.keys.push({ name, id, value: 'v' });
                draw();
                v.pulse(owner);
                v.log(`stored ${name} (${id}) on node ${owner}`, 'ok');
                say(`${name} → ID ${id} → stored on node ${owner}.`, 'ok');
            });
        });

        root.querySelector('[data-act="get"]').addEventListener('click', async () => {
            const name = keyInput.value.trim();
            if (!name || busy) return;
            const id = ringId(name);
            const from = +fromSel.value;
            await run(async tok => {
                const { path, owner } = route(st, from, id);
                await animateRoute(path, owner, id, tok);
                const found = st.keys.some(k => k.name === name);
                if (owner !== from) {
                    await v.packet(from, owner, `GET ${name}`, 'req', tok);
                    await v.packet(owner, from, found ? 'OK' : 'ERR not_found', found ? 'resp' : 'err', tok);
                } else v.log(`node ${from} is the owner: read locally`);
                const hops = path.length - 1;
                v.log(found ? `found ${name} on node ${owner}` : `node ${owner} owns ID ${id}, but ${name} was never stored`, found ? 'ok' : 'err');
                say(`${hops} forward${hops === 1 ? '' : 's'} to find the owner (node ${owner})${owner !== from ? ', then 1 direct GET' : ''}. ${found ? 'Found.' : 'Not found: try PUT first.'}`, found ? 'ok' : 'err');
            });
        });

        root.querySelector('[data-act="add"]').addEventListener('click', async () => {
            const name = nodeInput.value.trim() || `node-${Math.random().toString(36).slice(2, 6)}`;
            const id = ringId(name);
            if (st.nodes.some(n => n.id === id && n.alive)) { say(`${name} hashes to ${id}, which is already taken. Try another name.`, 'err'); return; }
            if (liveIds(st).length >= 12) { say('That’s plenty of nodes for a 256-ID ring. Crash one first.', 'err'); return; }
            await run(async tok => {
                const boot = liveIds(st)[0];
                st.nodes = st.nodes.filter(n => n.id !== id);
                const before = st.keys.map(k => [k.name, successorOf(st, k.id)]);
                st.nodes.push({ name, id, alive: true, color: colorNext++ % 8 });
                v.render(st, { arcs: false, focus: id });
                v.log(`hash("${escapeHtml(name)}") mod 256 = <b>${id}</b>`);
                if (boot != null && boot !== id) {
                    const succ = nextNode(st, id);
                    await v.packet(id, boot, `FIND_SUCCESSOR ${id}`, 'req', tok);
                    await v.packet(boot, id, `OK ${succ}`, 'resp', tok);
                    const moved = before.filter(([n, o]) => o !== successorOf(st, st.keys.find(k => k.name === n).id)).map(([n]) => n);
                    if (succ !== id) {
                        await v.packet(id, succ, 'TRANSFER_KEYS', 'req', tok);
                        if (moved.length) await v.packet(succ, id, `PUT_PRIMARY ×${moved.length}`, 'resp', tok);
                    }
                    v.log(moved.length ? `took over ${moved.join(', ')} from node ${succ}` : 'no keys needed to move', 'ok');
                    say(`${name} joined at ${id}. ${moved.length ? `${moved.length} key${moved.length > 1 ? 's' : ''} moved; every other key stayed put.` : 'No keys fell in its range.'}`, 'ok');
                }
                focus = id;
                nodeInput.value = '';
                draw();
            });
        });

        root.querySelector('[data-act="crash"]').addEventListener('click', async () => {
            if (focus == null) { say('Click a node on the ring first, then crash it.', 'err'); return; }
            if (liveIds(st).length <= 1) { say('Keep at least one node alive.', 'err'); return; }
            const id = focus;
            await run(async tok => {
                const pred = prevNode(st, id);
                st.nodes = st.nodes.filter(n => n.id !== id);
                focus = null;
                v.render(st, { arcs: false });
                await v.packet(pred, id, 'PING', 'err', tok).catch(() => {});
                const succ = successorOf(st, id);
                draw();
                v.log(`node ${id} removed. Its keys now belong to node ${succ}, which already held replicas.`, 'ok');
                say(`Node ${id} is gone. Node ${pred}'s successor is now ${succ}.`, 'ok');
            });
        });

        root.querySelector('[data-act="reset"]').addEventListener('click', () => {
            v.token++;
            st.nodes = mkNodes(BASE_NODES); st.keys = mkKeys(); colorNext = 4; focus = null; busy = false;
            v.clearLog(); v.clearPackets(); draw(); say('Reset to the four-node example.');
        });

        async function animateRoute(path, owner, id, tok) {
            v.log(`lookup ID ${id} from node ${path[0]}`);
            for (let k = 0; k < path.length - 1; k++) {
                await v.packet(path[k], path[k + 1], `FIND_SUCCESSOR ${id}`, 'req', tok);
                v.log(`${path[k]} → ${path[k + 1]} (closest preceding finger)`);
            }
            const last = path[path.length - 1];
            v.log(`${last}: ${id} ∈ (${last}, ${nextNode(st, last)}] → owner <b>${owner}</b>`);
            if (path.length > 1) await v.packet(last, path[0], `OK ${owner}`, 'resp', tok);
        }

        async function run(fn) {
            busy = true;
            const tok = ++v.token;
            v.clearPackets(); v.clearLog();
            root.querySelectorAll('.sb-controls button').forEach(b => b.disabled = true);
            try { await fn(tok); } catch (e) { if (e.message !== 'cancelled') console.error(e); }
            root.querySelectorAll('.sb-controls button').forEach(b => b.disabled = false);
            busy = false;
        }

        draw();
        say('Type a key, pick a start node, then PUT or GET. Click a node to see its fingers.');
    }

    function escapeHtml(s) {
        return s.replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
    }

    document.addEventListener('DOMContentLoaded', () => {
        const tour = document.getElementById('tour');
        if (tour) initTour(tour);
        const sb = document.getElementById('sandbox');
        if (sb) initSandbox(sb);
    });

    // exposed for testing in the console
    window.ChordSim = { stdHash, ringId, route, fingers, successorOf };
})();
