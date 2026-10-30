// The page a Hyperion LAN room serves to every browser on its network, at the room's
// address and port 7117: the room's password and a name; on a PC, the choice of playing in this
// tab (Hyperion for the browser, which the room serves at /play/, W3), streamed from the host, or
// installing the game from the host; then the lobby of the match the player is seated
// in, its chat, and the matches being played. Ported from the design's join.js, with the room's
// WebSocket (/ws, port_lanweb.cpp) in place of the design's stand-in data: the room says
// {"t":"room"} as it changes and once a second (port_lanroom.cpp's RoomJson; before joining,
// PublicJson), and this page says hello, race, ready, chat, sit, stand and status.
//
// What later stages bring is greyed "Not available yet": installing from the host, and the
// tournament.
//
// Steps (?step=): join, choose, lobby, watch, play, spectate. ?device= says what the page thinks it is on
// (windows, linux, arm, mac, phone, tablet, browser), from the user agent by default.
(function () {
  'use strict';
  var Q = new URLSearchParams(location.search);
  var top = document.getElementById('top'), app = document.getElementById('app');

  // ---- the kinds of device, as the design names them ---------------------------------------
  var KINDS = {
    windows: { name: 'Windows', long: 'Windows x64', glyph: 'desktop', pc: true },
    linux: { name: 'Linux', long: 'Linux x64', glyph: 'laptop', pc: true },
    arm: { name: 'Linux ARM', long: 'Linux ARM64', glyph: 'board', pc: true },
    mac: { name: 'Mac', long: 'macOS, Apple silicon', glyph: 'laptop', pc: true },
    phone: { name: 'Phone', long: 'Phone', glyph: 'phone' },
    tablet: { name: 'Tablet', long: 'Tablet', glyph: 'tablet' },
    browser: { name: 'Browser', long: 'Another device', glyph: 'tv' }
  };
  var GLYPH = {
    desktop: '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="3" y="4" width="18" height="12" rx="1.5"/><path d="M9 20h6M12 16v4"/></svg>',
    laptop: '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="5" y="5" width="14" height="10" rx="1.2"/><path d="M2.5 18.5h19l-1.5-3.5h-16z"/></svg>',
    phone: '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="7" y="2.5" width="10" height="19" rx="2"/><path d="M11 18.5h2"/></svg>',
    tablet: '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="4" y="3" width="16" height="18" rx="2"/><path d="M11 18h2"/></svg>',
    board: '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="4" y="4" width="16" height="16" rx="1.5"/><rect x="9" y="9" width="6" height="6"/><path d="M9 4V2M15 4V2M9 22v-2M15 22v-2M4 9H2M4 15H2M22 9h-2M22 15h-2"/></svg>',
    tv: '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="2.5" y="4" width="19" height="13" rx="1.2"/><path d="M8 21l4-4 4 4"/></svg>'
  };
  var ICON = {
    mark: '<svg viewBox="0 0 32 32" aria-hidden="true"><path d="M16 2.5 28 9.25v13.5L16 29.5 4 22.75V9.25z"/><path d="M16 8.5 22.5 12.2v7.6L16 23.5l-6.5-3.7v-7.6z"/></svg>',
    shield: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3 5 6v5c0 4.4 3 8.3 7 10 4-1.7 7-5.6 7-10V6z"/><path d="m9 12 2 2 4-4"/></svg>',
    tab: '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="3" y="5" width="18" height="14" rx="1.5"/><path d="M3 9h18M12 12v4M10 14l2 2 2-2"/></svg>',
    rotate: '<svg viewBox="0 0 64 64" aria-hidden="true"><rect x="22" y="8" width="20" height="36" rx="3"/><path d="M30 39h4"/><rect x="12" y="38" width="40" height="18" rx="3" opacity="0.55"/><path d="M50 20a14 14 0 0 1-4 14M50 34l-4 0 0-4"/></svg>'
  };
  var ROUTES = { stream: 'Streamed', install: 'Installed', tab: 'Browser' };
  // 1.17's player colours, each start's in a room's match
  var PC = ['#f40404', '#0c48cc', '#2cb494', '#88409c', '#f88c14', '#703014', '#cce0d0', '#fcfc38'];
  var COLORS = ['Red', 'Blue', 'Teal', 'Purple', 'Orange', 'Brown', 'White', 'Yellow'];
  var RACE = { terran: 'Terran', zerg: 'Zerg', protoss: 'Protoss', random: 'Random' };
  // a seat's race, and its handicap where it has one (StarCraft II's, 50 to 100%)
  function raceText(s) { return (RACE[s.race] || 'Random') + (s.handicap && s.handicap < 100 ? ' · ' + s.handicap + '%' : ''); }
  // the match's mutators in words (port_mutators.cpp's MutatorsText), '' when they are the original's
  function mutText(u) {
    if (!u) return '';
    var out = [], n = function (v) { return Number(v).toLocaleString('en-US'); };
    var share = function (p) { return '\u00d7' + (p / 100); };
    if (u.minerals !== undefined && u.minerals !== 100) out.push(u.minerals === 0 ? 'infinite mineral fields' : 'mineral fields ' + share(u.minerals));
    if (u.gas !== undefined && u.gas !== 100) out.push(u.gas === 0 ? 'infinite geysers' : 'geysers ' + share(u.gas));
    if ((u.startMinerals !== undefined && u.startMinerals !== 50) || (u.startGas !== undefined && u.startGas !== 0))
      out.push('start resources ' + n(u.startMinerals) + ' / ' + n(u.startGas));
    if (u.supply !== undefined && u.supply !== 200) out.push(u.supply === 0 ? 'no supply cap' : 'supply cap ' + n(u.supply));
    if (u.workers !== undefined && u.workers !== 4) out.push(u.workers + (u.workers === 1 ? ' start worker' : ' start workers'));
    if (u.boost) out.push('supply +' + u.boost + '%');
    if (u.fog === 1) out.push('the map explored');
    else if (u.fog === 2) out.push('no fog of war');
    if (u.build !== undefined && u.build !== 100) out.push('build time ' + share(u.build));
    if (u.tech) out.push(['', 'all researched', 'no upgrades', 'no research'][u.tech] || '');
    if (u.rich === 4) out.push('every base rich' + ((u.richRules || 0) & 8 ? ', not announced' : ''));
    else if (u.rich) {
      var rules = u.richRules === undefined ? 3 : u.richRules;
      var radius = u.richRadius === undefined ? 48 : u.richRadius;
      var far = radius >= 0 ? radius + ' tiles' : ({'-25': 'a quarter of the map', '-33': 'a third of the map',
        '-50': 'half the map'})[radius] || -radius + '% of the map';
      out.push('rich resources ' + ['', 'past the naturals', far + ' or more from every start', 'farthest only'][u.rich] + ' at ' +
               (u.richChance || 50) + '%' + (rules & 1 ? '' : ', each rolled alone') + (rules & 2 ? '' : ', maybe none') +
               (rules & 4 ? ', the same every game' : '') + (rules & 8 ? ', not announced' : ''));
    }
    if (u.units) out.push('unit limit ' + n(u.units));
    return out.filter(Boolean).join(', ');
  }
  var SPEED = { slowest: 'Slowest', slower: 'Slower', slow: 'Slow', normal: 'Normal', fast: 'Fast', faster: 'Faster', fastest: 'Fastest' };
  var LATER = 'Not available yet';

  // the user agent's guess; the choose step lets the player say otherwise
  function detect() {
    var ua = navigator.userAgent || '', touch = navigator.maxTouchPoints > 1;
    if (/iPhone|iPod|Android.+Mobile/.test(ua)) return 'phone';
    if (/iPad|Android/.test(ua) || (/Macintosh/.test(ua) && touch)) return 'tablet';
    if (/CrOS|SmartTV|Tizen|Web0S/.test(ua)) return 'browser';
    if (/Macintosh/.test(ua)) return 'mac';
    if (/Linux.+(aarch64|arm64)/.test(ua)) return 'arm';
    if (/Linux/.test(ua)) return 'linux';
    return 'windows';
  }
  // the browser, for the host's list of devices
  function browserName() {
    var ua = navigator.userAgent || '';
    var b = /Edg\//.test(ua) ? 'Edge' : /Firefox\//.test(ua) ? 'Firefox' : /Chrome\//.test(ua) ? 'Chrome' : /Safari\//.test(ua) ? 'Safari' : 'A browser';
    return b + ' on ' + KINDS[J.device].long;
  }

  var STEPS = ['join', 'choose', 'lobby', 'watch'];
  var dev0 = Q.get('device');
  var J = {
    step: 'join',
    device: KINDS[dev0] ? dev0 : detect(),
    name: '', pw: '', route: 'stream',
    room: null,       // the room's last word
    you: 0,           // this page's device in the room, once joined
    joined: false,
    refused: null,    // {why, fix}
    lost: false,      // the connection gone, being tried again
    wanted: STEPS.indexOf(Q.get('step')) >= 0 ? Q.get('step') : null,
    refresh: null     // the step's own update, as the room changes
  };
  function isPC(k) { return !!(KINDS[k] && KINDS[k].pc); }
  function isTouch(k) { return k === 'phone' || k === 'tablet'; }

  // ---- small pieces ------------------------------------------------------------------------
  function h(tag, cls, text) { var e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }
  function svg(markup, cls) { var s = h('span', cls || 'jn-glyph'); s.innerHTML = markup; return s; }
  function add(p) { for (var i = 1; i < arguments.length; i++) if (arguments[i]) p.appendChild(arguments[i]); return p; }
  function btn(label, cls, fn) { var b = h('button', cls, label); b.type = 'button'; if (fn) b.addEventListener('click', fn); return b; }
  function later(label, cls) { var b = btn(label, cls); b.disabled = true; b.title = LATER; return b; }
  function chip(route) { return h('span', 'jn-chip ' + route, ROUTES[route] || route); }
  function kindOf(k) { return KINDS[k] || KINDS.browser; }
  function devTag(kind) { var d = h('span', 'jn-dev'); d.appendChild(svg(GLYPH[kindOf(kind).glyph])); d.appendChild(document.createTextNode(kindOf(kind).name)); return d; }
  function band(rtt) { return rtt < 25 ? 'good' : rtt < 60 ? 'fair' : 'poor'; }
  function msTag(rtt) { return h('span', 'jn-ms ' + band(rtt), (rtt < 10 ? (+rtt).toFixed(1) : Math.round(rtt)) + ' ms'); }
  // the room's version as a player reads it: "Hyperion-v0.3.4-beta" is Hyperion 0.3.4-beta
  function versionText(v) { return v ? 'Hyperion ' + String(v).replace(/^Hyperion-?v?/i, '') : ''; }
  function clock(s) { s = Math.max(0, Math.floor(s)); var m = Math.floor(s / 60); return m + ':' + ('0' + (s % 60)).slice(-2); }
  function kindPhrase(k) { return { windows: 'a Windows PC', linux: 'a Linux PC', arm: 'an ARM computer running Linux', mac: 'a Mac', phone: 'a phone', tablet: 'a tablet', browser: 'a device with a browser' }[k]; }
  function devices() { return (J.room && J.room.devices) || []; }
  function device(id) { return devices().filter(function (d) { return d.id === id; })[0] || null; }
  function matches() { return (J.room && J.room.matches) || []; }
  function myMatch() {
    return matches().filter(function (m) {
      return m.state !== 'ended' && m.seats.some(function (s) { return s.owner === 'player' && s.dev === J.you; });
    })[0] || null;
  }

  // ---- the room's connection -----------------------------------------------------------------
  var ws = null;
  function connect() {
    try { ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws'); } catch (e) { return retry(); }
    ws.onopen = function () {
      // back after a drop: in again as before
      if (J.joined) hello();
    };
    ws.onmessage = function (e) {
      var m;
      try { m = JSON.parse(e.data); } catch (x) { return; }
      heard(m);
    };
    ws.onclose = function () { ws = null; retry(); };
  }
  function retry() {
    if (!J.lost) { J.lost = true; drawBanner(); }
    setTimeout(connect, 2000);
  }
  function say(m) { if (ws && ws.readyState === 1) ws.send(JSON.stringify(m)); }
  // the room's token for this player's seat, kept across a reload: the name alone takes no seat back
  function backToken() { try { return localStorage.getItem('hx-room-back') || ''; } catch (e) { return ''; } }
  function hello() {
    say({ t: 'hello', name: J.name, password: J.pw, kind: J.device, route: J.route, browser: browserName(),
          back: backToken() });
  }
  function heard(m) {
    if (m.t === 'ping') { say({ t: 'pong' }); return; }
    if (J.lost) { J.lost = false; drawBanner(); }
    if (m.t === 'room') {
      var first = !J.room;
      // the choose step's cards follow what the room serves for a browser
      var routes = !first && J.step === 'choose' && (!!J.room.play !== !!m.play || !!J.room.files !== !!m.files);
      J.room = m;
      if (m.name) document.title = m.name;
      if (first || routes) render(); else update();
      followGame();
      leaveFollow();
    } else if (m.t === 'rtc') {
      rtcHeard(m);
    } else if (m.t === 'tab') {
      tabHeard(m);
    } else if (m.t === 'tabrefused') {
      // the game in this tab is another build than the room's: said over its frame, with the stream
      if (TAB.wrap) {
        TAB.state = 'failed';
        TAB.why = m.why + (m.fix ? ' ' + m.fix : '');
        tabFollow();
      }
    } else if (m.t === 'welcome') {
      J.you = m.you;
      J.filesAt = m.files || '';  // the host's files for this page alone, once it is in
      if (m.back) try { localStorage.setItem('hx-room-back', m.back); } catch (e) {}
      var again = J.joined;
      J.joined = true;
      J.refused = null;
      // a seat on the host already playing for this player (the page opened again) offers anew
      askStream(0);
      if (!again) go(J.wanted && J.wanted !== 'join' && J.wanted !== 'play' ? J.wanted : isPC(J.device) ? 'choose' : 'lobby');
    } else if (m.t === 'refused') {
      J.joined = false;
      J.refused = { why: m.why, fix: m.fix };
      if (J.step !== 'join') go('join'); else update();
    } else if (m.t === 'error') {
      toast(m.text);
    } else if (m.t === 'closed') {
      J.joined = false;
      J.refused = { why: 'The host stopped the server.', fix: 'Join again when it starts.' };
      go('join');
    }
  }

  // ---- going between steps -------------------------------------------------------------------
  function go(step) {
    J.step = step;
    try {
      var q = new URLSearchParams(location.search);
      q.set('step', step);
      history.replaceState(null, '', location.pathname + '?' + q.toString());
    } catch (e) { /* a sandboxed frame */ }
    if (J.joined) say({ t: 'status', page: step, kind: J.device });
    render();
    window.scrollTo(0, 0);
  }
  function render() {
    J.refresh = null;
    stopStage();
    app.innerHTML = '';
    app.className = 'jn-main';
    drawTop();
    if (!J.room) { drawWaiting(); return; }
    var step = J.joined ? J.step : 'join';
    ({ join: drawJoin, choose: drawChoose, lobby: drawLobby, watch: drawWatch, play: drawPlay, spectate: drawSpectate })[step]();
    drawBanner();
  }
  function update() {
    drawTopLive();
    if (J.refresh) J.refresh();
  }
  function drawWaiting() {
    app.classList.add('narrow');
    var box = h('section', 'jn-box');
    box.appendChild(h('p', 'jn-kicker', 'A LAN room'));
    box.appendChild(h('h1', 'jn-h1', 'Finding the room…'));
    app.appendChild(box);
  }
  // the room not answering: said over the page until it does
  var banner = null;
  function drawBanner() {
    if (banner) { banner.remove(); banner = null; }
    if (!J.lost) return;
    banner = h('div', 'jn-banner', 'The room isn’t answering. Trying again…');
    banner.setAttribute('role', 'status');
    document.body.appendChild(banner);
  }
  var toastEl = null, toastT = 0;
  function toast(text) {
    if (toastEl) toastEl.remove();
    toastEl = h('div', 'jn-toast', text);
    toastEl.setAttribute('role', 'status');
    document.body.appendChild(toastEl);
    clearTimeout(toastT);
    toastT = setTimeout(function () { if (toastEl) { toastEl.remove(); toastEl = null; } }, 5000);
  }

  // the bar: the room, and once in, the tabs and who you are
  var meMs = null, watchDot = null;
  function drawTop() {
    top.innerHTML = '';
    meMs = watchDot = null;
    var r = J.room || {};
    var b = h('div', 'jn-brand');
    b.appendChild(svg(ICON.mark, 'jn-mark'));
    var t = h('div'); t.style.minWidth = '0';
    t.appendChild(h('b', null, r.name || 'Hyperion LAN'));
    t.appendChild(h('small', null, (r.machine || '') + (r.version ? ' · ' + versionText(r.version) : '')));
    b.appendChild(t);
    top.appendChild(b);
    if (!J.joined || (J.step !== 'lobby' && J.step !== 'watch')) return;
    var tabs = h('nav', 'jn-tabs');
    tabs.setAttribute('aria-label', 'The room');
    [['lobby', 'Lobby'], ['watch', 'Watch'], ['bracket', 'Tournament']].forEach(function (x) {
      var a = x[0] === 'bracket' ? later(x[1], 'jn-tab') : btn(x[1], 'jn-tab', function () { go(x[0]); });
      if (J.step === x[0]) a.setAttribute('aria-current', 'page');
      if (x[0] === 'watch') { watchDot = h('span', 'jn-dot live'); watchDot.title = 'Matches are live'; a.appendChild(watchDot); }
      tabs.appendChild(a);
    });
    top.appendChild(tabs);
    if (playing()) top.appendChild(btn('Back to the Game', 'jn-btn gold', function () { go('play'); }));
    else if (J.wasWatching) top.appendChild(btn('Back to the Match', 'jn-btn gold', function () { go('spectate'); }));
    var m = h('div', 'jn-me');
    m.appendChild(h('b', null, J.name));
    m.appendChild(chip(J.route));
    meMs = h('span');
    m.appendChild(meMs);
    top.appendChild(m);
    drawTopLive();
  }
  function drawTopLive() {
    if (meMs) { var d = device(J.you); meMs.innerHTML = ''; if (d) meMs.appendChild(msTag(d.rtt)); }
    if (watchDot) watchDot.hidden = !(J.room && J.room.live > 0);
  }

  // ---- join: the password and a name -----------------------------------------------------------
  function drawJoin() {
    var r = J.room;
    app.classList.add('narrow');
    var box = h('section', 'jn-box jn-join');
    box.setAttribute('aria-labelledby', 'jn-room');
    box.appendChild(h('p', 'jn-kicker', 'A LAN room'));
    var h1 = h('h1', 'jn-h1', r.name); h1.id = 'jn-room';
    box.appendChild(h1);
    box.appendChild(h('p', 'jn-meta', 'Hosted by ' + r.machine + ' · ' + location.host + ' · ' + versionText(r.version) + ' · ' + (r.game || 'Brood War')));
    var f = h('form');
    f.noValidate = true;
    var nf = field('Name', 'jn-name', 'text');
    nf.input.value = J.name || Q.get('name') || '';
    nf.input.placeholder = 'The name the others see';
    nf.input.autocomplete = 'nickname';
    nf.input.maxLength = 24;
    var pf = field('Password', 'jn-pw', 'password');
    pf.input.placeholder = 'Shown on the host’s screen';
    pf.input.autocomplete = 'off';
    add(f, nf.el, r.password ? pf.el : null);
    var go1 = h('button', 'jn-btn gold wide', 'Join');
    go1.type = 'submit';
    f.appendChild(go1);
    var gen = h('p', 'jn-err');
    gen.hidden = true;
    f.appendChild(gen);
    f.addEventListener('submit', function (e) {
      e.preventDefault();
      var name = nf.input.value.trim(), pw = pf.input.value;
      clearErr(nf); clearErr(pf); gen.hidden = true;
      if (!name) { showErr(nf, 'Choose a name to play under.'); nf.input.focus(); return; }
      if (r.password && !pw) { showErr(pf, 'Type the room’s password. The host shows it on its screen.'); pf.input.focus(); return; }
      J.name = name; J.pw = pw;
      go1.disabled = true;
      hello();
      setTimeout(function () { go1.disabled = false; }, 1500);
    });
    box.appendChild(f);
    var rules = h('div', 'jn-rules');
    rules.appendChild(svg(ICON.shield));
    rules.appendChild(h('p', 'jn-fine', 'Only on this network. Everyone playing needs their own copy of StarCraft.'));
    box.appendChild(rules);
    app.appendChild(box);
    // who is here, so a player sees they have the right room
    var hb = h('section', 'jn-box'), title = h('h2', 'jn-title'), list = h('div', 'jn-here');
    add(hb, title, list);
    app.appendChild(hb);
    function refused() {
      if (!J.refused) return;
      var why = J.refused.why || '', fix = J.refused.fix || '';
      if (/password/i.test(why)) showErr(pf, why);
      else if (/already here/i.test(why)) showErr(nf, why + ' ' + fix);
      else { gen.textContent = why + (fix ? ' ' + fix : ''); gen.hidden = false; }
    }
    J.refresh = function () {
      var here = (J.room.devices || []).filter(function (d) { return d.state !== 'refused' && !d.admin; });
      title.textContent = 'Here now · ' + here.length;
      list.innerHTML = '';
      here.forEach(function (d) { var s = h('span'); s.appendChild(svg(GLYPH[kindOf(d.kind).glyph])); s.appendChild(document.createTextNode(d.name)); list.appendChild(s); });
      if (!here.length) list.appendChild(h('p', 'jn-fine', 'Nobody yet.'));
      if (J.room.running === false) { gen.textContent = 'The room isn’t taking anyone now. Ask the host to start the server.'; gen.hidden = false; }
      refused();
    };
    J.refresh();
    setTimeout(function () { (nf.input.value ? pf.input : nf.input).focus(); }, 0);
  }
  function field(label, id, type) {
    var el = h('div', 'jn-field'), lb = h('label', null, label), inp = h('input', 'jn-input');
    lb.htmlFor = id; inp.id = id; inp.type = type;
    add(el, lb, inp);
    return { el: el, input: inp };
  }
  function showErr(f, text) {
    clearErr(f);
    var p = h('p', 'jn-err', text); p.id = f.input.id + '-err';
    f.input.setAttribute('aria-invalid', 'true'); f.input.setAttribute('aria-describedby', p.id);
    f.el.appendChild(p);
  }
  function clearErr(f) { var p = f.el.querySelector('.jn-err'); if (p) p.remove(); f.input.removeAttribute('aria-invalid'); f.input.removeAttribute('aria-describedby'); }

  // ---- choose: on a PC, play in the browser or install ------------------------------------------
  function drawChoose() {
    app.classList.add('mid');
    var head = h('div', 'jn-detect');
    var a = h('div');
    a.appendChild(h('p', 'jn-kicker', 'Welcome, ' + J.name));
    a.appendChild(h('h1', 'jn-h2', 'This looks like ' + kindPhrase(J.device)));
    head.appendChild(a);
    var nf = h('div', 'jn-field'), lb = h('label', null, 'Not right?'), sel = h('select', 'jn-select');
    lb.htmlFor = sel.id = 'jn-kind';
    Object.keys(KINDS).forEach(function (id) { var o = h('option', null, KINDS[id].long); o.value = id; if (id === J.device) o.selected = true; sel.appendChild(o); });
    sel.addEventListener('change', function () { J.device = sel.value; say({ t: 'status', kind: J.device }); if (isPC(J.device)) render(); else go('lobby'); });
    add(nf, lb, sel);
    head.appendChild(nf);
    app.appendChild(head);
    var cards = h('div', 'jn-cards');
    // the game in this tab where the browser can run it and the host has it; the host's stream
    // otherwise, and as the other choice
    var tab = canTab() && J.room && J.room.play;
    if (tab) {
      var t = card('tab', 'Play in This Tab', ['Nothing to install: the game runs in this browser.',
        hostFiles() ? 'StarCraft’s files from the host, or from your own folder, kept in this browser.'
                     : 'StarCraft’s files from your own folder, kept in this browser.',
        'Your seat in the lobby, its chat and the matches being played are here.'],
        btn('Go to the Lobby', 'jn-btn gold', function () { setRoute('tab'); go('lobby'); }));
      t.classList.add('pick');
      cards.appendChild(t);
    }
    var s = card('stream', tab ? 'Streamed from the Host' : 'Play in the Browser', ['Nothing to install.',
      'The host runs your game and sends it to this tab as video.',
      tab ? 'For a browser or a device that cannot run the game itself.' : 'Your seat in the lobby, its chat and the matches being played are here.'],
      btn('Go to the Lobby', tab ? 'jn-btn' : 'jn-btn gold', function () { setRoute('stream'); go('lobby'); }));
    if (!tab) s.classList.add('pick');
    cards.appendChild(s);
    var c = card('install', 'Install for Full Quality', ['Hyperion for ' + KINDS[J.device].long + ', from this host.', 'Then the game data from this host, checked against its own.',
      'Plays on your PC at its own resolution and refresh rate.'], later(LATER, 'jn-btn'));
    c.classList.add('off');
    c.querySelector('.jn-card-head').appendChild(h('span', 'jn-tag-later', LATER));
    cards.appendChild(c);
    app.appendChild(cards);
    var n = h('p', 'jn-fine', 'Hyperion already installed? Choose Multiplayer, then Join a Room: this room is listed there.');
    n.style.marginTop = '18px';
    app.appendChild(n);
    // the files and settings this browser keeps for the game in its tab, saved to a file or
    // loaded from one, in a tab of their own
    if (tab) {
      var sp = h('p', 'jn-fine');
      sp.appendChild(document.createTextNode('Playing in this tab before? '));
      var a = h('a', null, 'This browser’s files and settings');
      a.href = '/play/?setup=1' + (hostFiles() ? '&offer=' + encodeURIComponent(hostFiles()) : '');
      a.target = '_blank';
      a.rel = 'noopener';
      sp.appendChild(a);
      sp.appendChild(document.createTextNode(': where the files come from, and the game’s settings, saved to a file or loaded from one.'));
      app.appendChild(sp);
    }
  }
  function card(route, title, lines, button) {
    var c = h('section', 'jn-card');
    var hd = h('div', 'jn-card-head');
    hd.appendChild(chip(route));
    c.appendChild(hd);
    c.appendChild(h('h3', null, title));
    if (lines) { var ul = h('ul'); lines.forEach(function (l) { ul.appendChild(h('li', null, l)); }); c.appendChild(ul); }
    c.appendChild(button);
    return c;
  }

  // ---- lobby: the match being set up --------------------------------------------------------------
  function drawLobby() {
    var grid = h('div', 'jn-lobby');
    var side = h('div', 'jn-side');
    // the seats: the match this page is seated in, or the matches being set up to sit in
    var box = h('section', 'jn-box jn-seatbox');
    var head = h('div', 'jn-lobby-head'), a = h('div'), kicker = h('p', 'jn-kicker'), title = h('h1', 'jn-h2'), meta = h('p', 'jn-meta'), muts = h('p', 'jn-fine');
    add(a, kicker, title, meta, muts);
    var count = h('span', 'jn-chip');
    add(head, a, count);
    var seatsEl = h('div', 'jn-seats'), closed = h('p', 'jn-closed');
    add(box, head, seatsEl, closed);
    var keep = h('div', 'jn-keep');
    keep.appendChild(svg(ICON.tab));
    keep.appendChild(h('p', 'jn-fine', 'Playing in the browser is not available yet, so a match with this seat cannot start. Your seat, its chat and the matches being played are here meanwhile.'));
    box.appendChild(keep);
    // chat, under the seats
    var cb = h('section', 'jn-box jn-chat');
    cb.appendChild(h('h2', 'jn-title', 'Chat'));
    var lines = h('div', 'jn-chat-lines');
    lines.setAttribute('role', 'log'); lines.setAttribute('aria-live', 'polite');
    var cf = h('form'), ci = h('input', 'jn-input');
    ci.type = 'text'; ci.placeholder = 'Say something to the match'; ci.setAttribute('aria-label', 'Chat'); ci.maxLength = 120;
    var send = h('button', 'jn-mini', 'Send'); send.type = 'submit';
    add(cf, ci, send);
    cf.addEventListener('submit', function (e) {
      e.preventDefault();
      var t = ci.value.trim(), m = myMatch();
      if (!t || !m) return;
      say({ t: 'chat', match: m.id, text: t });
      ci.value = '';
    });
    add(cb, lines, cf);
    // the map, and your own choices
    var mb = h('section', 'jn-box');
    mb.appendChild(h('h2', 'jn-title', 'The Map'));
    var pic = h('canvas', 'jn-pic');
    pic.width = pic.height = 480;
    pic.setAttribute('role', 'img');
    var picNote = h('p', 'jn-fine', 'The map’s picture: ' + LATER.toLowerCase() + '.');
    add(mb, pic, picNote);
    drawGround(pic);
    var yb = h('section', 'jn-box jn-yours');
    yb.appendChild(h('h2', 'jn-title', 'Your Seat'));
    var mine = h('div', 'jn-mine');
    var rf = h('div', 'jn-field'), rl = h('label', null, 'Race'), rs = h('select', 'jn-select');
    rl.htmlFor = rs.id = 'jn-race';
    ['terran', 'zerg', 'protoss', 'random'].forEach(function (r) { var o = h('option', null, RACE[r]); o.value = r; rs.appendChild(o); });
    rs.addEventListener('change', function () { say({ t: 'race', race: rs.value }); });
    add(rf, rl, rs);
    rf.style.gridColumn = '1 / -1';
    // the handicap: your units and buildings at a share of their life and shields
    var hcf = h('div', 'jn-field'), hcl = h('label', null, 'Handicap'), hcs = h('select', 'jn-select');
    hcl.htmlFor = hcs.id = 'jn-handicap';
    [100, 90, 80, 70, 60, 50].forEach(function (n) { var o = h('option', null, n + '%'); o.value = String(n); hcs.appendChild(o); });
    hcs.title = 'Your units and buildings at this share of their life and shields, to even a game between players of different skill. Below 100% the match plays by Hyperion’s rules.';
    hcs.addEventListener('change', function () { say({ t: 'handicap', handicap: Number(hcs.value) }); });
    add(hcf, hcl, hcs);
    hcf.style.gridColumn = '1 / -1';
    var colf = h('div', 'jn-field'), coll = h('span', null, 'Colour'), sws = h('div', 'jn-swatches');
    coll.style.cssText = 'font-size:13px;font-weight:600;color:var(--lb-ink-opt)';
    colf.style.gridColumn = '1 / -1';
    sws.setAttribute('role', 'group'); sws.setAttribute('aria-label', 'Colour');
    var colNote = h('p', 'jn-fine', 'Each start keeps its colour in a room’s match. Choosing one: ' + LATER.toLowerCase() + '.');
    add(colf, coll, sws, colNote);
    // how this browser plays, where the room offers both: the game in this tab, or streamed from
    // the host; changed between matches (the room moves the seat, its status's route)
    var hf = h('div', 'jn-field'), hl = h('span', null, 'How You Play'), hw = h('div', 'jn-row');
    hl.style.cssText = coll.style.cssText;
    hf.style.gridColumn = '1 / -1';
    hw.setAttribute('role', 'group');
    hw.setAttribute('aria-label', 'How you play');
    var hTab = btn('In This Tab', 'jn-mini', function () { setRoute('tab'); });
    var hStream = btn('Streamed from the Host', 'jn-mini', function () { setRoute('stream'); });
    var hNote = h('p', 'jn-fine', '');
    add(hw, hTab, hStream);
    add(hf, hl, hw, hNote);
    var ready = btn('Ready', 'jn-btn gold', function () { var d = device(J.you); say({ t: 'ready', on: !(d && d.ready) }); });
    var stand = btn('Leave the Seat', 'jn-link', function () { say({ t: 'stand' }); });
    add(mine, rf, hcf, colf, hf, ready, stand);
    yb.appendChild(mine);
    add(side, mb, yb);
    add(grid, box, side, cb);
    app.appendChild(grid);

    // the map's picture, which the host draws and the room serves (m.pic), with each start's
    // number in its seat's colour; the ground drawn for one while it has none
    var picKey = '';
    function drawMap(m) {
      var key = m && m.pic ? m.pic + '|' + m.seats.map(function (s, i) { return s.owner + (s.colour >= 0 ? s.colour : i); }).join(',') : '';
      if (key === picKey && (key || picKey === '')) { if (key || pic.dataset.ground) return; }
      picKey = key;
      if (!key) {
        if (!pic.dataset.ground) { drawGround(pic); pic.dataset.ground = '1'; }
        pic.setAttribute('aria-label', m ? (m.mapName || m.name) + ', its picture not here yet' : 'No map yet');
        picNote.hidden = !!m;
        return;
      }
      var img = new Image();
      img.onload = function () {
        if (picKey !== key) return;
        delete pic.dataset.ground;
        var g = pic.getContext('2d'), n = pic.width, k = Math.min(n / img.width, n / img.height);
        var w = img.width * k, hh = img.height * k, x0 = (n - w) / 2, y0 = (n - hh) / 2;
        g.fillStyle = '#000';
        g.fillRect(0, 0, n, n);
        g.imageSmoothingEnabled = false;
        g.drawImage(img, x0, y0, w, hh);
        (m.picStarts || []).forEach(function (st) {
          var s = m.seats[st[0]];
          if (!s || s.owner === 'closed') return;
          var cx = x0 + st[1] * w, cy = y0 + st[2] * hh, c = PC[(s.colour >= 0 ? s.colour : st[0]) % PC.length], open = s.owner === 'open';
          g.beginPath();
          g.arc(cx, cy, 13, 0, Math.PI * 2);
          g.fillStyle = open ? 'rgba(0, 0, 0, 0.65)' : c;
          g.fill();
          g.lineWidth = 2;
          g.strokeStyle = open ? c : '#000';
          g.stroke();
          g.fillStyle = open ? '#fff' : '#000';
          g.font = 'bold 14px sans-serif';
          g.textAlign = 'center';
          g.textBaseline = 'middle';
          g.fillText(String(st[0] + 1), cx, cy + 1);
        });
      };
      img.src = m.pic;
      pic.setAttribute('aria-label', (m.mapName || m.name) + ', its minimap with the starts numbered');
      picNote.hidden = true;
    }

    function drawHow(me) {
      var both = canTab() && !!(J.room && J.room.play), playing = !!(me && me.state === 'playing');
      hf.hidden = !both;
      hTab.setAttribute('aria-pressed', String(J.route === 'tab'));
      hStream.setAttribute('aria-pressed', String(J.route === 'stream'));
      hTab.disabled = hStream.disabled = playing;
      hNote.textContent = playing ? 'You can change this once the match is over.'
        : J.route === 'tab' ? 'The game runs in this tab, from StarCraft’s files kept in this browser.'
                            : 'The host runs your game and sends it to this tab as video.';
    }

    var shownChat = '';
    J.refresh = function () {
      var m = myMatch(), me = device(J.you);
      drawHow(me);
      seatsEl.innerHTML = '';
      if (!m) {
        // not seated: the matches being set up, with a seat to take
        drawMap(null);
        kicker.textContent = 'In the room';
        title.textContent = 'Waiting for a Match';
        var open = matches().filter(function (x) { return x.state === 'lobby'; });
        meta.textContent = open.length ? 'Take a seat in a match being set up.' : 'The host hasn’t set up a match yet. You’ll be seated when they do.';
        count.textContent = (J.room.people || 0) + ' in the room';
        closed.textContent = '';
        open.forEach(function (x) {
          var free = x.seats.map(function (s, i) { return s.owner === 'open' ? i : -1; }).filter(function (i) { return i >= 0; });
          var r = h('div', 'jn-seat');
          var who = h('div', 'jn-who');
          who.appendChild(h('b', null, x.name + (x.mapName && x.mapName !== x.name ? ' · ' + x.mapName : '')));
          who.appendChild(h('div', 'jn-row', free.length ? free.length + (free.length === 1 ? ' open seat' : ' open seats') : 'No open seats'));
          var z = h('div', 'jn-seat-z');
          var take = btn('Take a Seat', 'jn-mini', function () { say({ t: 'sit', match: x.id, slot: -1 }); });
          take.disabled = !free.length;
          z.appendChild(take);
          add(r, who, z);
          seatsEl.appendChild(r);
        });
        lines.innerHTML = '';
        shownChat = '';
        ready.disabled = rs.disabled = hcs.disabled = true;
        stand.hidden = true;
        drawSwatches(-1, []);
        return;
      }
      kicker.textContent = 'Being set up by the host';
      title.textContent = m.mapName || m.name;
      meta.textContent = [m.type === 'melee' ? 'Melee' : m.type, SPEED[m.speed] || m.speed, (m.rules || J.room.rules || 'classic') + ' rules', m.mapPlayers + ' starts'].filter(Boolean).join(' · ');
      var mt = mutText(m.mutators);
      muts.textContent = mt ? 'Mutators, the host\u2019s: ' + mt + '.' : '';
      muts.hidden = !mt;
      drawMap(m);
      var on = 0, shut = [], mySlot = -1, taken = [];
      m.seats.forEach(function (s, i) {
        if (s.owner === 'closed') { if (i < m.mapPlayers) shut.push(i + 1); return; }
        var colour = s.colour >= 0 ? s.colour : i;
        var you = s.owner === 'player' && s.dev === J.you;
        if (you) mySlot = i; else if (s.owner !== 'open') taken.push(colour);
        var r = h('div', 'jn-seat' + (you ? ' you' : '') + (s.owner === 'open' ? ' empty' : ''));
        var n = h('span', 'jn-num', String(i + 1));
        n.style.setProperty('--c', PC[colour % PC.length]);
        r.appendChild(n);
        var who = h('div', 'jn-who'), z = h('div', 'jn-seat-z');
        if (s.owner === 'open') {
          who.appendChild(h('b', null, 'Open'));
          z.appendChild(h('span', null, 'Waiting for a player'));
        } else {
          on++;
          var d = s.owner === 'player' ? device(s.dev) : null;
          who.appendChild(h('b', null, (s.owner === 'computer' ? 'Computer' : s.name || (d && d.name) || 'A player') + (you ? ' (you)' : '')));
          var row = h('div', 'jn-row');
          if (s.owner === 'computer') row.appendChild(h('span', 'jn-chip cpu', 'Run by the host'));
          else if (d) {
            row.appendChild(chip(d.admin ? 'install' : d.route));
            row.appendChild(devTag(d.admin ? 'windows' : d.kind));
            row.appendChild(msTag(d.rtt));
          }
          who.appendChild(row);
          z.appendChild(h('span', null, raceText(s)));
          var rd = s.owner === 'computer' || s.ready;
          z.appendChild(h('span', 'jn-ready' + (rd ? ' on' : ''), rd ? 'Ready' : 'Not ready'));
        }
        add(r, who, z);
        seatsEl.appendChild(r);
      });
      count.textContent = on + ' of ' + m.mapPlayers + ' seated';
      closed.textContent = shut.length ? (shut.length === 1 ? 'Start ' + shut[0] + ' is closed.' : 'Starts ' + shut.join(', ').replace(/, (\d+)$/, ' and $1') + ' are closed.') : '';
      // your seat
      var lobby = m.state === 'lobby';
      ready.disabled = rs.disabled = hcs.disabled = !lobby;
      stand.hidden = !lobby;
      if (me) {
        if (document.activeElement !== rs) rs.value = me.race || 'random';
        if (document.activeElement !== hcs) hcs.value = String(me.handicap || 100);
        ready.textContent = me.ready ? 'Not Ready' : 'Ready';
      }
      drawSwatches(mySlot >= 0 ? (m.seats[mySlot].colour >= 0 ? m.seats[mySlot].colour : mySlot) : -1, taken);
      // the chat, redrawn only when it has changed
      var text = JSON.stringify(m.chat || []);
      if (text !== shownChat) {
        shownChat = text;
        lines.innerHTML = '';
        (m.chat || []).forEach(function (x) { var l = h('div', x[0] ? null : 'sys'); if (x[0]) l.appendChild(h('b', null, x[0] + ': ')); l.appendChild(document.createTextNode(x[1])); lines.appendChild(l); });
        if (!(m.chat || []).length) lines.appendChild(h('div', 'sys', 'Nothing said yet.'));
        lines.scrollTop = lines.scrollHeight;
      }
    };
    function drawSwatches(mineColour, taken) {
      sws.innerHTML = '';
      PC.forEach(function (c, i) {
        var b = btn('', 'jn-swatch');
        b.style.setProperty('--c', c);
        b.setAttribute('aria-label', COLORS[i] + (i === mineColour ? ', yours' : taken.indexOf(i) >= 0 ? ', taken' : ''));
        b.setAttribute('aria-pressed', String(i === mineColour));
        // yours shown as chosen; the others not to be had yet
        if (i === mineColour) b.setAttribute('aria-disabled', 'true'); else b.disabled = true;
        b.title = i === mineColour ? 'Your start’s colour' : LATER;
        sws.appendChild(b);
      });
    }
    J.refresh();
  }
  // the map's place: a ground from noise until the room sends the map's picture
  function drawGround(cv) {
    var g = cv.getContext('2d'), n = cv.width, c = document.createElement('canvas');
    c.width = c.height = 128;
    var cg = c.getContext('2d'), img = cg.createImageData(128, 128), seed = 11, N = 9, gr = [];
    function rnd() { seed = (Math.imul(seed, 22695477) + 1) >>> 0; return ((seed >>> 16) & 0x7fff) / 32768; }
    for (var i = 0; i < N * N; i++) gr.push(rnd());
    function at(x, y) { return gr[Math.min(N - 1, y) * N + Math.min(N - 1, x)]; }
    for (var y = 0; y < 128; y++) for (var x = 0; x < 128; x++) {
      var gx = x / 128 * (N - 1), gy = y / 128 * (N - 1), ix = Math.floor(gx), iy = Math.floor(gy), fx = gx - ix, fy = gy - iy;
      var v = (at(ix, iy) * (1 - fx) + at(ix + 1, iy) * fx) * (1 - fy) + (at(ix, iy + 1) * (1 - fx) + at(ix + 1, iy + 1) * fx) * fy;
      v = v * 0.8 + rnd() * 0.2;
      var col = v < 0.28 ? [22, 44, 70] : v < 0.33 ? [70, 66, 40] : v < 0.62 ? [38, 62, 30] : v < 0.8 ? [52, 74, 34] : [86, 78, 52], o = (y * 128 + x) * 4;
      img.data[o] = col[0]; img.data[o + 1] = col[1]; img.data[o + 2] = col[2]; img.data[o + 3] = 255;
    }
    cg.putImageData(img, 0, 0);
    g.imageSmoothingEnabled = false;
    g.drawImage(c, 0, 0, 128, 128, 0, 0, n, n);
    g.fillStyle = 'rgba(0, 0, 0, 0.35)'; g.fillRect(0, 0, n, n);
  }

  // ---- play: the game a seat on the host plays for this browser ----------------------------------
  // The host runs this player's game in a Hyperion of its own, with no window, and sends it here
  // over WebRTC: the picture, the sound, and a data channel the mouse and keys go back on, in the
  // game's pixels (port_lanstream.cpp). The room passes the two sides' descriptions between them.
  // The page becomes the video, letterboxed into the window at the stream's own size.
  var RTC = { pc: null, dc: null, stream: null, state: 'new', wantT: 0 };
  var QUALITY = { High: 6000, Balanced: 3000, Saver: 1200 };
  // in the player's game, or on the screens after it (the score, which waits for its Continue)
  function playing() { return !!J.wasPlaying; }
  // the game starting for this player opens the stream; the score screen after it is played there too,
  // and once it has been continued (the seat on the host says so every two seconds) the lobby comes back
  function followGame() {
    if (J.route === 'tab') { tabFollow(); return; }
    var d = device(J.you), inGame = !!(d && d.state === 'playing' && J.route === 'stream');
    // watching: a seat on the host watches the match for this page; the room ends it when the match
    // has been shown to its end, and the matches come back
    var watching = !!(d && d.state === 'watching');
    if (watching && !J.wasWatching && J.step !== 'spectate') go('spectate');
    else if (!watching && J.wasWatching && J.step === 'spectate') { J.wasWatching = false; go('watch'); }
    J.wasWatching = watching;
    if (inGame) J.afterGame = 0;
    else if (J.inGame) J.afterGame = Date.now();
    J.inGame = inGame;
    if (J.afterGame && Date.now() - J.afterGame > 6000 && !(d && d.stream && d.stream.score)) J.afterGame = 0;
    var now = inGame || !!J.afterGame;
    if (now && !J.wasPlaying && J.step !== 'play') go('play');
    else if (!now && J.wasPlaying && J.step === 'play') go('lobby');
    else if (now !== J.wasPlaying && J.step !== 'play') render();
    J.wasPlaying = now;
  }
  function rtcHeard(m) {
    if (m.type !== 'offer' || !m.sdp) return;
    if (RTC.pc) try { RTC.pc.close(); } catch (e) { /* closed already */ }
    var pc = RTC.pc = new RTCPeerConnection({ iceServers: [] });
    RTC.dc = null;
    RTC.state = 'connecting';
    RTC.stream = new MediaStream();
    if (ST) showStream(ST);
    pc.ontrack = function (e) {
      if (pc !== RTC.pc) return;
      RTC.stream.addTrack(e.track);
      if (ST) showStream(ST);
    };
    pc.ondatachannel = function (e) {
      if (pc !== RTC.pc) return;
      RTC.dc = e.channel;
      RTC.dc.onopen = function () { send('q ' + QUALITY[J.quality || 'Balanced']); };
    };
    pc.onconnectionstatechange = function () {
      if (pc !== RTC.pc) return;
      RTC.state = pc.connectionState;
      if (pc.connectionState === 'failed') askStream(2000);
    };
    pc.setRemoteDescription({ type: 'offer', sdp: m.sdp })
      .then(function () { return pc.createAnswer(); })
      .then(function (a) { return pc.setLocalDescription(a); })
      .then(function () { return gathered(pc); })
      .then(function () { if (pc === RTC.pc) say({ t: 'rtc', type: 'answer', sdp: pc.localDescription.sdp }); })
      .catch(function (e) { toast('The stream could not start: ' + ((e && e.message) || e)); });
  }
  // the browser's addresses, all of them, before the answer goes: a LAN needs no trickle
  function gathered(pc) {
    return new Promise(function (ok) {
      if (pc.iceGatheringState === 'complete') return ok();
      var t = setTimeout(ok, 3000);
      pc.addEventListener('icegatheringstatechange', function () {
        if (pc.iceGatheringState === 'complete') { clearTimeout(t); ok(); }
      });
    });
  }
  // ---- the game in this tab (W3) ------------------------------------------------------------------
  // Hyperion itself, compiled for the browser, which the room serves at /play/: its page in a frame
  // behind this one, started with the seat's token the room sends (a message {t: 'tab'}), its files
  // the host's when it offers them and the player chose them, or the player's own folder. The frame
  // comes over this page while the player plays or watches, and while it has its files to choose.
  var TAB = { wrap: null, frame: null, hud: null, note: null, token: '', state: '', title: '', text: '', frac: 0, game: '', why: '' };
  // while the game is over the page, the browser's zoom is kept off it here too, over the bar
  // above it (the frame keeps it off the game itself, play.js)
  window.addEventListener('wheel', function (e) {
    if (e.ctrlKey && TAB.wrap && TAB.wrap.classList.contains('on')) e.preventDefault();
  }, { passive: false });
  // whether this browser runs 64-bit WebAssembly, which Hyperion is built as: a module with one
  // where the host offers its own files to this page ('' when it does not): a token of the page's
  // own, which the room gives once the page is in
  function hostFiles() {
    return J.room && J.room.files && J.filesAt ? J.filesAt : '';
  }
  // 64-bit memory, validated
  function canTab() {
    try { return WebAssembly.validate(new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0, 5, 3, 1, 4, 1])); } catch (e) { return false; }
  }
  function tabHeard(m) {
    if (J.route !== 'tab' || !m.token) return;
    if (TAB.frame && TAB.token === m.token && TAB.state !== 'failed') return;
    tabStop();
    TAB.token = m.token;
    var args = ['--join', '127.0.0.1:' + m.port, '--lan-name', J.name, '--lan-tab', J.you + ':' + m.token];
    var q = 'embed=1&args=' + encodeURIComponent(JSON.stringify(args));
    if (hostFiles()) q += '&offer=' + encodeURIComponent(hostFiles());
    var wrap = TAB.wrap = h('div', 'jn-tab');
    var f = TAB.frame = h('iframe');
    f.title = 'Hyperion';
    f.src = '/play/?' + q;
    f.allow = 'fullscreen; autoplay';
    wrap.appendChild(f);
    var hud = TAB.hud = h('div', 'jn-hud');
    hud.hidden = true;
    wrap.appendChild(hud);
    document.body.appendChild(wrap);
    TAB.note = h('div', 'jn-tabnote');
    TAB.note.setAttribute('role', 'status');
    document.body.appendChild(TAB.note);
    TAB.state = 'loading';
    tabFollow();
  }
  function tabStop() {
    if (TAB.wrap) TAB.wrap.remove();
    if (TAB.note) TAB.note.remove();
    TAB.wrap = TAB.frame = TAB.hud = TAB.note = null;
    TAB.token = TAB.state = TAB.game = TAB.why = '';
  }
  window.addEventListener('message', function (e) {
    if (e.origin !== location.origin || !TAB.frame || e.source !== TAB.frame.contentWindow || !e.data) return;
    if (e.data.hy === 'state') {
      if (TAB.why) return;  // refused by the room, which the frame's own state does not undo
      TAB.state = e.data.state;
      TAB.title = e.data.title;
      TAB.text = e.data.text;
      TAB.frac = e.data.frac;
    } else if (e.data.hy === 'game') {
      TAB.game = e.data.where;
    } else if (e.data.hy === 'note') {
      // what the game's page saw of its connections, for the room's log
      if (J.joined) say({ t: 'note', text: String(e.data.text).slice(0, 300) });
      return;
    } else if (e.data.hy === 'exit') {
      // Exit Program in the game: back to this page, and the room gives the tab a new Hyperion
      // for the next match (StartTab)
      tabStop();
      return;
    }
    tabFollow();
  });
  // the frame over the page, or behind it, by the room's word of this player and the frame's own
  function tabFollow() {
    if (!TAB.wrap) return;
    var d = device(J.you), st = d ? d.state : '';
    var needs = TAB.state === 'choose' || TAB.state === 'failed';
    var on = needs || st === 'playing' || st === 'watching' || TAB.game === 'game';
    var was = TAB.wrap.classList.contains('on');
    TAB.wrap.classList.toggle('on', on);
    if (on && !was) try { TAB.frame.focus(); } catch (e) { /* not yet loaded */ }
    // over the frame: Leave while watching; the stream instead, where the files cannot be had
    TAB.hud.innerHTML = '';
    TAB.hud.hidden = !(st === 'watching' || needs);
    if (st === 'watching') {
      var wm = d.match ? matches().filter(function (x) { return x.id === d.match; })[0] : null;
      var bug = h('span', 'jn-bug');
      add(bug, h('span', 'jn-dot live'), document.createTextNode(J.big ? 'The Big Screen' : 'Live'));
      TAB.hud.appendChild(bug);
      if (J.room && J.room.delay) TAB.hud.appendChild(h('span', 'jn-behind', J.room.delay + ' s behind'));
      if (wm) TAB.hud.appendChild(h('b', null, wm.name + (wm.mapName ? ' · ' + wm.mapName : '')));
      var push = h('span', 'jn-push');
      push.appendChild(btn('Leave', 'jn-mini', function () { say({ t: 'unwatch' }); }));
      TAB.hud.appendChild(push);
    } else if (needs) {
      TAB.hud.appendChild(h('span', null, TAB.state === 'failed' ? TAB.why || 'The game stopped in this tab.' : 'The game needs StarCraft’s files.'));
      var p2 = h('span', 'jn-push');
      p2.appendChild(btn('Play Streamed Instead', 'jn-mini', function () { setRoute('stream'); }));
      TAB.hud.appendChild(p2);
    }
    // below the page: how far the game has got in starting
    var ready = d && d.seat;
    TAB.note.hidden = on || !!ready || !TAB.state;
    TAB.note.textContent = TAB.state === 'fetching' || TAB.state === 'copying'
      ? TAB.title + ': ' + TAB.text + ' (' + Math.round(TAB.frac * 100) + '%)'
      : 'Getting your game ready in this tab…';
  }
  // In a match, leaving the page costs the player: in the tab the game goes with it, and the
  // seat with it, which the match counts as a forfeit; streamed, the seat plays on unplayed
  // until the player joins again by the same name. The page says so as the match starts, and
  // the browser asks before a refresh, a close or another address (its own words: a page may
  // no longer give the browser's dialog a text of its own).
  var LEAVE = { on: false, el: null };
  function inMatch() { var d = device(J.you); return !!(J.joined && d && d.state === 'playing'); }
  function leaveFollow() {
    var on = inMatch();
    if (on === LEAVE.on) return;
    LEAVE.on = on;
    if (LEAVE.el) { LEAVE.el.remove(); LEAVE.el = null; }
    if (!on) return;
    var el = LEAVE.el = h('div', 'jn-leave', J.route === 'tab'
      ? 'You’re in a match. Refreshing this page, closing it or going to another address forfeits your place in it.'
      : 'You’re in a match. Refreshing this page, closing it or going to another address leaves your seat unplayed until you join again with the same name.');
    el.setAttribute('role', 'status');
    document.body.appendChild(el);
    setTimeout(function () { el.classList.add('fade'); }, 8000);
    setTimeout(function () { if (LEAVE.el === el) { el.remove(); LEAVE.el = null; } }, 8800);
  }
  window.addEventListener('beforeunload', function (e) {
    if (!inMatch()) return;
    e.preventDefault();
    e.returnValue = '';
  });
  function setRoute(route) {
    if (J.route === route) return;
    J.route = route;
    if (route !== 'tab') tabStop();
    say({ t: 'status', route: route, kind: J.device });
    render();
  }

  function askStream(ms) {
    clearTimeout(RTC.wantT);
    RTC.wantT = setTimeout(function () { if (J.joined) say({ t: 'rtc', want: true }); }, ms || 0);
  }
  function send(line) { if (RTC.dc && RTC.dc.readyState === 'open') RTC.dc.send(line); }

  var ST = null;
  function stopStage() {
    if (!ST) return;
    var st = ST;
    st.alive = false;
    clearTimeout(st.hideT);
    clearInterval(st.statT);
    letGo(st);
    window.removeEventListener('resize', st.onResize);
    document.removeEventListener('keydown', st.onKeyDown, true);
    document.removeEventListener('keyup', st.onKeyUp, true);
    document.removeEventListener('pointerlockchange', st.onLock);
    window.removeEventListener('blur', st.onBlur);
    if (document.pointerLockElement && document.exitPointerLock) document.exitPointerLock();
    if (navigator.keyboard && navigator.keyboard.unlock) try { navigator.keyboard.unlock(); } catch (e) { /* not locked */ }
    if (document.fullscreenElement && document.exitFullscreen) document.exitFullscreen().catch(function () {});
    st.el.remove();
    document.body.style.overflow = '';
    ST = null;
  }
  function stage(o) {
    var el = h('div', 'jn-stage');
    el.setAttribute('role', 'application');
    el.setAttribute('aria-label', o.label);
    var v = h('video', 'jn-video');
    v.autoplay = true;
    v.muted = true;  // until a click lets it sound
    v.playsInline = true;
    v.setAttribute('playsinline', '');
    el.appendChild(v);
    var joining = h('div', 'jn-joining', 'Starting the stream…');
    el.appendChild(joining);
    document.body.appendChild(el);
    document.body.style.overflow = 'hidden';
    var st = ST = { el: el, video: v, alive: true, touch: isTouch(J.device), o: o, joining: joining, res: [640, 480],
      px: 320, py: 240, keys: {}, buttons: {} };
    v.addEventListener('resize', function () {
      if (v.videoWidth) { st.res = [v.videoWidth, v.videoHeight]; fit(st); }
    });
    v.addEventListener('playing', function () {
      if (st.joining) { st.joining.remove(); st.joining = null; }
    });
    st.onResize = function () { fit(st); if (st.touch) turnCheck(st); };
    window.addEventListener('resize', st.onResize);
    fit(st);
    if (RTC.stream && RTC.state !== 'failed' && RTC.state !== 'closed') showStream(st); else askStream(0);
    return st;
  }
  function showStream(st) {
    if (st.video.srcObject !== RTC.stream) st.video.srcObject = RTC.stream;
    var p = st.video.play();
    // sound needs a click the browser counts as the player's; refused, the picture goes on silent
    if (p && p.catch) p.catch(function () {
      if (!st.video.muted) { st.video.muted = true; st.video.play().catch(function () {}); }
    });
  }
  function fit(st) {
    var W = window.innerWidth, H = window.innerHeight, k = Math.min(W / st.res[0], H / st.res[1]);
    st.k = k;
    st.video.style.width = Math.round(st.res[0] * k) + 'px';
    st.video.style.height = Math.round(st.res[1] * k) + 'px';
  }
  // a point on the picture, in the game's pixels
  function gamePoint(st, e) {
    var r = st.video.getBoundingClientRect();
    var x = Math.floor((e.clientX - r.left) * st.res[0] / Math.max(1, r.width));
    var y = Math.floor((e.clientY - r.top) * st.res[1] / Math.max(1, r.height));
    return [Math.max(0, Math.min(st.res[0] - 1, x)), Math.max(0, Math.min(st.res[1] - 1, y))];
  }
  function key(st, vk, down, ch) {
    if (down) { st.keys[vk] = 1; send('k ' + vk + ' 1 ' + (ch || 0)); }
    else if (st.keys[vk]) { delete st.keys[vk]; send('k ' + vk + ' 0'); }
  }
  // what this page holds down, let go: the window losing the focus, or the mouse let go
  function letGo(st) {
    Object.keys(st.keys).forEach(function (vk) { send('k ' + vk + ' 0'); });
    Object.keys(st.buttons).forEach(function (b) { send('u ' + b + ' ' + st.px + ' ' + st.py); });
    st.keys = {};
    st.buttons = {};
  }
  // the bar over the stream: what it is, how it is going, and the way out. It goes after 3 seconds of
  // nothing, and comes back on a mouse move or a touch
  function hud(st, o) {
    var bar = h('div', 'jn-hud');
    o.left(bar);
    var push = h('div', 'jn-push');
    if (o.quality) {
      var qs = h('select', 'jn-select');
      qs.setAttribute('aria-label', 'Quality');
      [['High', 'Quality: High'], ['Balanced', 'Quality: Balanced'], ['Saver', 'Quality: Data saver']].forEach(function (x) {
        var op = h('option', null, x[1]); op.value = x[0]; if (x[0] === (J.quality || 'Balanced')) op.selected = true; qs.appendChild(op);
      });
      qs.addEventListener('change', function () { J.quality = qs.value; send('q ' + QUALITY[qs.value]); });
      push.appendChild(qs);
    }
    push.appendChild(btn('Leave', 'jn-mini', o.leave));
    bar.appendChild(push);
    st.el.appendChild(bar);
    function wake() {
      bar.classList.remove('hide');
      clearTimeout(st.hideT);
      st.hideT = setTimeout(function () { if (!bar.matches(':hover, :focus-within')) bar.classList.add('hide'); }, 3000);
    }
    st.el.addEventListener('mousemove', function () { if (!st.captured) wake(); });
    st.el.addEventListener('touchstart', wake, { passive: true });
    if (o.autohide) wake();
    return bar;
  }
  function sep() { return h('span', 'sep', '·'); }

  function drawPlay() {
    var m = myMatch();
    var st = stage({ label: 'The game, streamed from ' + (J.room.machine || 'the host') });
    hud(st, {
      autohide: true, quality: true, leave: function () { send('l'); go('lobby'); },
      left: function (bar) {
        var s = h('div', 'jn-stats');
        var src = h('span'); src.appendChild(document.createTextNode('Streamed from ')); src.appendChild(h('b', null, J.room.machine || 'the host'));
        var rtt = h('span', null, '…'), rate = h('span', null, '…'), res = h('span', 'opt');
        add(s, src, sep(), rtt, sep(), rate, h('span', 'sep opt', '·'), res);
        bar.appendChild(s);
        // the readings, from the browser's own statistics: the round trip, the bits a second, the size
        var last = null;
        st.statT = setInterval(function () {
          res.textContent = st.res[0] + ' × ' + st.res[1];
          if (!RTC.pc) return;
          RTC.pc.getStats().then(function (r) {
            var bytes = 0, ms = null, at = performance.now();
            r.forEach(function (x) {
              if (x.type === 'inbound-rtp' && x.kind === 'video') { bytes = x.bytesReceived; st.frames = x.framesDecoded; }
              if (x.type === 'candidate-pair' && x.nominated && x.currentRoundTripTime != null) ms = x.currentRoundTripTime * 1000;
            });
            if (ms != null) rtt.textContent = (ms < 10 ? ms.toFixed(1) : Math.round(ms)) + ' ms';
            if (last) rate.textContent = ((bytes - last.bytes) * 8 / 1000 / Math.max(1, at - last.at)).toFixed(1) + ' Mbit/s';
            last = { bytes: bytes, at: at };
          }).catch(function () {});
        }, 1000);
      }
    });
    if (st.touch) touchLayer(st); else mouseCapture(st, m);
  }
  // spectate: a live match the room's delay behind, from a seat on the host that watches it with
  // Replay TV's director at the camera; the players across the top, no input. The Big Screen goes
  // between the live matches where the fighting is, and says which. A click or a tap lets it sound
  function drawSpectate() {
    var st = stage({ label: J.big ? 'Watching the Big Screen' : 'Watching a match' });
    var bug = h('span', 'jn-bug'), behind = h('span', 'jn-behind'), title = h('b'), players = h('div', 'jn-specplayers');
    hud(st, {
      autohide: false, leave: function () { say({ t: 'unwatch' }); J.wasWatching = false; go('watch'); },
      left: function (bar) {
        var s = h('div', 'jn-stats');
        add(bug, h('span', 'jn-dot live'), document.createTextNode(J.big ? 'The Big Screen' : 'Live'));
        add(s, bug, behind, title, players);
        bar.appendChild(s);
      }
    });
    var shown = null;
    J.refresh = function () {
      var d = device(J.you), m = d && d.match ? matches().filter(function (x) { return x.id === d.match; })[0] : null;
      behind.textContent = (J.room.delay || 10) + ' s behind';
      var key = m ? m.id + ':' + m.seats.map(function (x) { return x.owner + x.name + x.race; }).join(',') : '';
      if (key === shown) return;
      shown = key;
      title.textContent = m ? m.name : 'Waiting for a match';
      // the Big Screen between matches
      if (!m && !st.waiting) {
        st.waiting = h('div', 'jn-joining', 'Nothing is being played. The Big Screen goes to the next match as it starts.');
        st.el.appendChild(st.waiting);
      } else if (m && st.waiting) {
        st.waiting.remove();
        st.waiting = null;
      }
      players.innerHTML = '';
      if (m) m.seats.forEach(function (x, i) {
        if (x.owner !== 'player' && x.owner !== 'computer') return;
        var p = h('span'), sw = h('span', 'jn-sw');
        sw.style.background = PC[(x.colour >= 0 ? x.colour : i) % PC.length];
        add(p, sw, document.createTextNode(x.owner === 'computer' ? 'Computer' : x.name || 'A player'), h('small', null, raceText(x)));
        players.appendChild(p);
      });
    };
    J.refresh();
    st.el.addEventListener('click', function () {
      if (st.video.muted) { st.video.muted = false; st.video.play().catch(function () { st.video.muted = true; }); }
    });
  }
  // on a PC: the mouse is held inside the game, and the keys the browser would take go to the game
  // while it is. Pointer lock holds the mouse; the keyboard lock, where the browser has one, holds
  // Ctrl+W and the like, and needs the page full screen
  function mouseCapture(st, m) {
    var card = over(st, 'Click to Play', 'The match is on. You are ' + J.name + (m ? ', in ' + m.name : '') + '.', [
      ['Mouse', 'Stays inside the game while you play'], ['Esc', 'Lets the mouse go'], ['Ctrl+W', 'And the browser’s other keys go to the game while the mouse is held']],
      'Click to Play', capture);
    st.onLock = function () {
      if (!document.pointerLockElement && st.captured && st.alive) {
        st.captured = false; st.el.classList.remove('captured');
        letGo(st);
        card = over(st, 'The Mouse Is Free', 'The game goes on without you holding it. Click to go back in.', null, 'Back to the Game', capture);
      }
    };
    document.addEventListener('pointerlockchange', st.onLock);
    st.onBlur = function () { letGo(st); };
    window.addEventListener('blur', st.onBlur);
    // the mouse: where it is on the picture, or, held by the pointer lock, moved by its own amounts
    function point(e) {
      if (document.pointerLockElement === st.video) {
        var r = st.video.getBoundingClientRect();
        st.px = Math.max(0, Math.min(st.res[0] - 1, st.px + e.movementX * st.res[0] / Math.max(1, r.width)));
        st.py = Math.max(0, Math.min(st.res[1] - 1, st.py + e.movementY * st.res[1] / Math.max(1, r.height)));
      } else {
        var p = gamePoint(st, e);
        st.px = p[0]; st.py = p[1];
      }
      return Math.round(st.px) + ' ' + Math.round(st.py);
    }
    st.el.addEventListener('mousemove', function (e) { if (st.captured) send('m ' + point(e)); });
    st.el.addEventListener('mousedown', function (e) {
      if (!st.captured || e.target.closest('.jn-hud, .jn-over')) return;
      e.preventDefault();
      st.buttons[e.button] = 1;
      send((e.detail > 1 && e.detail % 2 === 0 ? 'D ' : 'd ') + e.button + ' ' + point(e));
    });
    st.el.addEventListener('mouseup', function (e) {
      if (!st.buttons[e.button]) return;
      delete st.buttons[e.button];
      send('u ' + e.button + ' ' + point(e));
    });
    st.el.addEventListener('contextmenu', function (e) { e.preventDefault(); });
    st.onKeyDown = function (e) {
      if (!st.captured || e.key === 'Escape' && document.pointerLockElement) return;
      e.preventDefault();
      key(st, e.keyCode, true, e.key.length === 1 ? e.key.charCodeAt(0) : 0);
    };
    st.onKeyUp = function (e) { if (st.keys[e.keyCode]) { e.preventDefault(); key(st, e.keyCode, false); } };
    document.addEventListener('keydown', st.onKeyDown, true);
    document.addEventListener('keyup', st.onKeyUp, true);
    function capture() {
      if (card) { card.remove(); card = null; }
      st.captured = true; st.el.classList.add('captured');
      st.video.muted = false;
      showStream(st);
      try { var p = st.el.requestFullscreen && st.el.requestFullscreen(); if (p && p.then) p.then(function () { if (navigator.keyboard && navigator.keyboard.lock) navigator.keyboard.lock().catch(function () {}); }).catch(function () {}); } catch (e) { /* not allowed here */ }
      try { var q = st.video.requestPointerLock && st.video.requestPointerLock(); if (q && q.catch) q.catch(function () {}); } catch (e) { /* not allowed here */ }
      var held = h('div', 'jn-held', 'Esc lets the mouse go');
      st.el.appendChild(held);
      setTimeout(function () { held.classList.add('fade'); }, 2600);
      setTimeout(function () { held.remove(); }, 3400);
    }
  }
  function over(st, title, line, keys, label, fn, extra) {
    var o = h('div', 'jn-over');
    var b = h('div', 'jn-box');
    b.setAttribute('role', 'dialog'); b.setAttribute('aria-label', title);
    b.appendChild(h('h2', 'jn-h2', title));
    if (line) { var p = h('p', 'jn-fine', line); p.style.marginTop = '8px'; b.appendChild(p); }
    if (keys) { var dl = h('dl', 'jn-keys'); keys.forEach(function (k) { dl.appendChild(h('dt', null, k[0])); dl.appendChild(h('dd', null, k[1])); }); b.appendChild(dl); }
    var go1 = btn(label, 'jn-btn gold wide', fn);
    b.appendChild(go1);
    if (extra) b.appendChild(extra);
    o.appendChild(b);
    st.el.appendChild(o);
    setTimeout(function () { go1.focus(); }, 0);
    return o;
  }
  // on a phone or tablet: taps for clicks, a drag for a box, two fingers to move the screen (the
  // arrow keys, held the way the fingers go), and a bar at the left edge for the keys a touch screen
  // does not have
  function touchLayer(st) {
    var card = null;
    st.afterTurn = function () {
      if (card || st.shownTouch) return;
      st.shownTouch = true;
      card = over(st, 'Touch Controls', null, [['Tap', 'A left click'], ['Two-finger tap', 'A right click'], ['Drag', 'A selection box'],
        ['Two-finger drag', 'Moves the screen'], ['Shift, Ctrl, Keys', 'At the left edge, for hotkeys']], 'Play', function () {
        card.remove(); card = null;
        st.video.muted = false;
        showStream(st);
      });
    };
    var bar = h('div', 'jn-touchbar'), pad = null;
    [['Shift', 16], ['Ctrl', 17]].forEach(function (k) {
      var b = btn(k[0], 'jn-mini', function () {
        var on = b.getAttribute('aria-pressed') !== 'true';
        b.setAttribute('aria-pressed', String(on));
        key(st, k[1], on);
      });
      b.setAttribute('aria-pressed', 'false');
      bar.appendChild(b);
    });
    var keysBtn = btn('Keys', 'jn-mini', function () {
      if (pad) { pad.remove(); pad = null; keysBtn.setAttribute('aria-pressed', 'false'); return; }
      keysBtn.setAttribute('aria-pressed', 'true');
      pad = h('div', 'jn-pad');
      pad.setAttribute('role', 'group'); pad.setAttribute('aria-label', 'Hotkeys');
      'QWERTASDFGZXCVB12345'.split('').forEach(function (k) {
        var b = btn(k, 'jn-mini', function () {
          var shift = !!st.keys[16];
          key(st, k.charCodeAt(0), true, (shift ? k : k.toLowerCase()).charCodeAt(0));
          key(st, k.charCodeAt(0), false);
          b.classList.add('sent'); setTimeout(function () { b.classList.remove('sent'); }, 300);
        });
        pad.appendChild(b);
      });
      st.el.appendChild(pad);
    });
    keysBtn.setAttribute('aria-pressed', 'false');
    bar.appendChild(keysBtn);
    st.el.appendChild(bar);
    // the gestures, read from pointer events on the picture
    var pts = {}, start = null, box = null, moved = false, two = null;
    function at(e) { var r = st.el.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top, g: gamePoint(st, e) }; }
    function mark(p, right) { var m = h('div', 'jn-tapmark' + (right ? ' right' : '')); m.style.left = p.x + 'px'; m.style.top = p.y + 'px'; st.el.appendChild(m); setTimeout(function () { m.remove(); }, 520); }
    function click(g, b) { send('m ' + g[0] + ' ' + g[1]); send('d ' + b + ' ' + g[0] + ' ' + g[1]); send('u ' + b + ' ' + g[0] + ' ' + g[1]); }
    // the screen moved by the arrow keys, held while the fingers are away from where they began
    function scroll(dx, dy) {
      var want = { 37: dx > 30, 39: dx < -30, 38: dy > 30, 40: dy < -30 };
      Object.keys(want).forEach(function (vk) { if (want[vk] !== !!st.keys[vk]) key(st, +vk, want[vk]); });
    }
    st.video.addEventListener('pointerdown', function (e) {
      pts[e.pointerId] = at(e);
      var ids = Object.keys(pts);
      if (ids.length === 1) { start = pts[e.pointerId]; moved = false; two = null; }
      else if (ids.length === 2) {
        if (box) { box.remove(); box = null; send('u 0 ' + start.g[0] + ' ' + start.g[1]); }
        var a = pts[ids[0]], b = pts[ids[1]];
        two = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2, moved: false, g: a.g };
      }
      try { st.video.setPointerCapture(e.pointerId); } catch (x) { /* fine */ }
    });
    st.video.addEventListener('pointermove', function (e) {
      if (!pts[e.pointerId]) return;
      pts[e.pointerId] = at(e);
      var ids = Object.keys(pts);
      if (two && ids.length === 2) {
        var a = pts[ids[0]], b = pts[ids[1]], mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
        if (Math.hypot(mx - two.x, my - two.y) > 8) two.moved = true;
        scroll(mx - two.x, my - two.y);
        return;
      }
      if (!start || two) return;
      var p = pts[e.pointerId];
      if (!moved && Math.hypot(p.x - start.x, p.y - start.y) > 10) {
        moved = true;
        box = h('div', 'jn-selbox'); st.el.appendChild(box);
        send('m ' + start.g[0] + ' ' + start.g[1]);
        send('d 0 ' + start.g[0] + ' ' + start.g[1]);
      }
      if (box) {
        box.style.left = Math.min(p.x, start.x) + 'px'; box.style.top = Math.min(p.y, start.y) + 'px';
        box.style.width = Math.abs(p.x - start.x) + 'px'; box.style.height = Math.abs(p.y - start.y) + 'px';
        send('m ' + p.g[0] + ' ' + p.g[1]);
      }
    });
    function up(e) {
      var p = pts[e.pointerId];
      delete pts[e.pointerId];
      var left = Object.keys(pts).length;
      if (two) {
        if (left === 0) {
          scroll(0, 0);
          if (!two.moved) { mark({ x: two.x, y: two.y }, true); click(two.g, 2); }
          two = null; start = null;
        }
        return;
      }
      if (box) { box.remove(); box = null; if (p) send('u 0 ' + p.g[0] + ' ' + p.g[1]); }
      else if (p && start && !moved) { mark(p, false); click(p.g, 0); }
      start = null;
    }
    st.video.addEventListener('pointerup', up);
    st.video.addEventListener('pointercancel', up);
    turnCheck(st);
  }
  // a phone held upright gets a card asking it to turn: the game is wider than it is tall
  function turnCheck(st) {
    var upright = J.device === 'phone' && window.innerHeight > window.innerWidth;
    if (upright && !st.turn && !st.turnWaved) {
      var o = h('div', 'jn-over'), b = h('div', 'jn-box jn-turn');
      b.setAttribute('role', 'dialog'); b.setAttribute('aria-label', 'Turn your phone sideways');
      b.appendChild(svg(ICON.rotate));
      b.appendChild(h('h2', 'jn-h2', 'Turn Your Phone Sideways'));
      b.appendChild(h('p', 'jn-fine', 'The game is wider than it is tall. Turned, it fills the screen.'));
      b.appendChild(btn('Play Upright Anyway', 'jn-mini', function () { st.turnWaved = true; o.remove(); st.turn = null; if (st.afterTurn) st.afterTurn(); }));
      o.appendChild(b);
      st.el.appendChild(o);
      st.turn = o;
    } else if (!upright && st.turn) { st.turn.remove(); st.turn = null; }
    if (!st.turn && st.afterTurn) st.afterTurn();
  }

  // ---- watch: the matches being played --------------------------------------------------------------
  function drawWatch() {
    var head = h('div');
    head.appendChild(h('p', 'jn-kicker', 'Live on ' + J.room.machine));
    head.appendChild(h('h1', 'jn-h2', 'Watch a Match'));
    var p = h('p', 'jn-meta');
    head.appendChild(p);
    head.style.marginBottom = '18px';
    app.appendChild(head);
    var grid = h('div', 'jn-matches');
    app.appendChild(grid);
    var shown = null;
    J.refresh = function () {
      var live = matches().filter(function (m) { return m.state === 'live'; });
      p.textContent = J.room.watching === false ? 'The host has turned watching off.' :
        (J.room.delay || 10) + ' seconds behind the players, so nobody watching can give a game away.';
      var key = live.map(function (m) { return m.id + ':' + m.watchers.length; }).join(',');
      if (key !== shown) {
        shown = key;
        grid.innerHTML = '';
        live.forEach(function (m) {
          var c = h('section', 'jn-match');
          var meta = h('div', 'jn-match-meta'), t = h('span', 'jn-ms');
          t.dataset.match = m.id;
          add(meta, h('span', 'jn-dot live'), t, h('span', null, 'in · ' + (m.mapName || m.name) + ' · ' + (SPEED[m.speed] || m.speed)));
          c.appendChild(meta);
          c.appendChild(h('h3', null, m.name));
          c.appendChild(playersOf(m));
          var w = m.watchers.length;
          c.appendChild(h('p', 'jn-fine', w ? w + ' watching' : 'Nobody watching yet'));
          c.appendChild(watchBtn('Watch', 'jn-btn gold', { t: 'watch', match: m.id }, false));
          grid.appendChild(c);
        });
        if (!live.length) {
          var none = h('section', 'jn-box');
          none.appendChild(h('p', 'jn-fine', 'Nothing is being played now. The matches appear here as they start.'));
          grid.appendChild(none);
        }
        var cast = h('section', 'jn-match cast');
        var cm = h('div', 'jn-match-meta'); add(cm, h('span', 'jn-dot live'), h('span', null, 'Every live match'));
        cast.appendChild(cm);
        cast.appendChild(h('h3', null, 'The Big Screen'));
        cast.appendChild(h('p', 'jn-fine', 'The caster’s feed: it goes to the biggest fight in any live match, and says which.'));
        cast.appendChild(watchBtn('Watch the Big Screen', 'jn-btn', { t: 'watch', big: true }, true));
        grid.appendChild(cast);
        var fine = h('p', 'jn-fine', 'With Hyperion installed, Watch on a live match in the room watches it in the game.');
        fine.style.gridColumn = '1 / -1';
        grid.appendChild(fine);
      }
      // the clocks
      live.forEach(function (m) {
        var t = grid.querySelector('[data-match="' + m.id + '"]');
        if (t) t.textContent = clock(m.since || m.frame / 24);
      });
    };
    J.refresh();
  }
  // Watch: the room starts a seat on the host that watches for this page, and the page follows it
  function watchBtn(label, cls, msg, big) {
    var me = device(J.you), off = J.room.watching === false ? 'The host has turned watching off.' :
      me && me.state === 'playing' ? 'You are playing in a match.' : null;
    var b = btn(label, cls, function () { J.big = big; say(msg); });
    if (off) { b.disabled = true; b.title = off; }
    return b;
  }
  // the players by side: each team one in a team game, each player one in a melee
  function playersOf(m) {
    var box = h('div', 'jn-players'), teams = {}, melee = m.type === 'melee' || m.type === 'ffa';
    m.seats.forEach(function (s, i) {
      if (s.owner !== 'player' && s.owner !== 'computer') return;
      s.slot = i;
      var side = melee ? 's' + i : 't' + s.team;
      (teams[side] = teams[side] || []).push(s);
    });
    Object.keys(teams).forEach(function (t, i) {
      if (i) box.appendChild(h('span', 'jn-vs', 'against'));
      teams[t].forEach(function (s) {
        var d = s.owner === 'player' ? device(s.dev) : null, r = h('div', 'jn-player');
        var sw = h('span', 'jn-sw'); sw.style.background = PC[(s.colour >= 0 ? s.colour : s.slot) % PC.length];
        add(r, sw, h('b', null, s.owner === 'computer' ? 'Computer' : s.name || (d && d.name) || 'A player'), h('small', null, raceText(s)));
        if (d) { r.appendChild(chip(d.admin ? 'install' : d.route)); r.appendChild(devTag(d.admin ? 'windows' : d.kind)); }
        box.appendChild(r);
      });
    });
    return box;
  }

  // for checks: the page's state, and a way to join without typing
  window.HyJoin = {
    state: function () { return { step: J.step, joined: J.joined, you: J.you, refused: J.refused, room: J.room, lost: J.lost }; },
    stream: function () {
      var v = ST && ST.video;
      return { rtc: RTC.state, channel: RTC.dc ? RTC.dc.readyState : 'none', tracks: RTC.stream ? RTC.stream.getTracks().length : 0,
        w: v ? v.videoWidth : 0, h: v ? v.videoHeight : 0, frames: ST ? ST.frames || 0 : 0, captured: !!(ST && ST.captured) };
    },
    send: function (line) { send(line); },
    tab: function () { return { state: TAB.state, game: TAB.game, on: !!(TAB.wrap && TAB.wrap.classList.contains('on')) }; },
    pc: function () { return RTC.pc; },
    join: function (name, pw) { J.name = name; J.pw = pw; hello(); },
    go: go
  };
  render();
  connect();
})();
