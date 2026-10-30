// Hyperion in the browser: Hyperion compiled to WebAssembly (tools/build_engine_web.py),
// run from the player's own StarCraft 1.17 files. The page keeps them in the browser's storage
// (Emscripten's IDBFS, in IndexedDB) at /hyperion, the program's own folder there, with Hyperion's
// data folder, Data/ (Hyperion/ before 0.6.2's last build, which the program moves across),
// beside them (port_posix.cpp's ExePath, port_paths.cpp); nothing is uploaded.
//
// The files come from the folder the player chooses (by the browser's folder picker where it
// has one, which asks to let the page view the folder rather than to upload it), from files
// picked one by one, the setup listing each with what it is, or from ?files=<address>, which the page
// fetches each one from (a LAN room's, when its host serves them). ?env=NAME=value,... sets the
// program's environment and ?args= its command line, for tests.
//
// The room's join page runs this page in a frame for a player who plays in the tab
// (?embed=1), telling it the room's seat to take in ?args; the page tells the frame's parent how
// it stands (a message {hy: 'state', ...}). ?offer=<address> is where the host offers its own
// files, which the setup offers in turn, and the player's choice is kept in this browser
// (localStorage, 'hyperion.play'); ?setup=1 shows the setup though the files are kept. The
// browser's settings, that choice and the game's own settings.json, go to a file and come back
// from one.
(function () {
  'use strict';
  var Q = new URLSearchParams(location.search);
  var EMBED = Q.get('embed') === '1';
  var OFFER = Q.get('offer') || '';
  var PREF_KEY = 'hyperion.play';
  // the files Hyperion reads, by their names in a 1.17 install; the install CD's two archives
  // (the music and the movies) are optional
  var NEED = ['Local.dll', 'StarDat.mpq', 'BrooDat.mpq', 'patch_rt.mpq'];
  var MORE = ['Starcraft.mpq', 'BroodWar.mpq'];
  // each file as the setup lists it, with what it is
  var WHAT = {
    'local.dll': ['Local.dll', 'the game’s language resources'],
    'stardat.mpq': ['StarDat.mpq', 'StarCraft’s art and sounds'],
    'broodat.mpq': ['BrooDat.mpq', 'Brood War’s art and sounds'],
    'patch_rt.mpq': ['Patch_rt.mpq', 'the 1.17 patch'],
    'starcraft.mpq': ['Starcraft.mpq', 'the music and movies, from the install CD'],
    'broodwar.mpq': ['BroodWar.mpq', 'Brood War’s music and movies, from its CD']
  };
  function known(name) { return Object.prototype.hasOwnProperty.call(WHAT, name.toLowerCase()); }
  var MAP_FILE = /\.(scm|scx|hxm|rep)$/i;
  var ROOT = '/hyperion';
  var $ = function (id) { return document.getElementById(id); };
  var M = null;
  var LOG = [];
  window.HyPlay = { log: LOG, state: 'starting', module: function () { return M; } };

  // how the page stands, to the join page that runs it in a frame
  function tell(state, title, text, frac) {
    if (!EMBED || window.parent === window) return;
    try {
      window.parent.postMessage({ hy: 'state', state: state, title: title || '', text: text || '', frac: frac || 0 }, location.origin);
    } catch (e) { /* the parent has gone */ }
  }
  function say(state, title, text, frac) {
    window.HyPlay.state = state;
    $('setup').hidden = true;
    $('loading').hidden = false;
    $('loading-title').textContent = title;
    $('loading-text').textContent = text || '';
    $('loading-bar').style.width = Math.round((frac || 0) * 100) + '%';
    tell(state, title, text, frac);
  }
  // the choices this browser keeps; storage may be refused (a private window), and then the
  // page asks each time
  function readPref() {
    try { return JSON.parse(localStorage.getItem(PREF_KEY) || '{}') || {}; } catch (e) { return {}; }
  }
  function writePref(p) {
    try { localStorage.setItem(PREF_KEY, JSON.stringify(p)); } catch (e) { /* not kept */ }
  }
  function line(kind, text) {
    LOG.push(text);
    if (LOG.length > 2000) LOG.shift();
    (kind === 'err' ? console.warn : console.log)('[hyperion] ' + text);
  }
  function sync(fromStore) {
    return new Promise(function (ok, bad) { M.FS.syncfs(fromStore, function (e) { if (e) bad(e); else ok(); }); });
  }
  // the name a file has in the folder, whatever its case
  function have(name) {
    var want = name.toLowerCase(), names = [];
    try { names = M.FS.readdir(ROOT); } catch (e) { return null; }
    for (var i = 0; i < names.length; i++) if (names[i].toLowerCase() === want) return names[i];
    return null;
  }
  function missing() { return NEED.filter(function (n) { return !have(n); }); }

  // ---- the files ------------------------------------------------------------------------------
  function put(name, bytes) {
    var old = have(name);
    if (old) M.FS.unlink(ROOT + '/' + old);
    M.FS.writeFile(ROOT + '/' + name, bytes);
  }
  // a file in a folder under the program's, the folders made as needed
  function putUnder(rel, bytes) {
    var parts = rel.split('/').filter(Boolean), dir = ROOT;
    for (var i = 0; i < parts.length - 1; i++) {
      dir += '/' + parts[i];
      try { M.FS.mkdir(dir); } catch (e) { /* there already */ }
    }
    M.FS.writeFile(ROOT + '/' + parts.join('/'), bytes);
  }
  // The folder, by the browser's folder picker where it has one (a page on localhost or https:
  // the browser asks to let the page view the folder's files), or else by a folder input, which
  // Chrome words as uploading the files to the site though the page only reads them here. Only
  // the files the setup lists and the maps under maps/ are read.
  function walk(dir, prefix, out) {
    var it = dir.values();
    function step() {
      return it.next().then(function (r) {
        if (r.done) return out;
        var h = r.value, rel = prefix + h.name;
        if (h.kind === 'directory')
          return (prefix || /^maps$/i.test(h.name) ? walk(h, rel + '/', out) : Promise.resolve()).then(step);
        if (prefix ? MAP_FILE.test(h.name) : known(h.name))
          return h.getFile().then(function (f) { out.push({ rel: rel, file: f }); return step(); });
        return step();
      });
    }
    return step();
  }
  function pickFolder() {
    if (!window.showDirectoryPicker) { $('pick-folder').click(); return; }
    window.showDirectoryPicker({ id: 'starcraft', mode: 'read' }).then(function (dir) {
      say('copying', 'Reading the Folder', dir.name, 0);
      return walk(dir, '', []).then(function (list) { takeFiles(list, dir.name); });
    }, function (e) {
      if (!e || e.name !== 'AbortError') pickError('The folder could not be read: ' + ((e && e.message) || e) + '.');
    }).catch(function (e) { pickError('The folder could not be read: ' + ((e && e.message) || e) + '.'); });
  }
  // the setup again, saying what went wrong, or how the files stand
  function pickError(text) {
    choose(missing());
    $('setup-error').hidden = false;
    $('setup-error').textContent = text;
  }
  // the files picked, each by its place under the folder chosen (its own name, for files
  // picked one by one); folder is the folder's name, or '' for files picked one by one
  function takeFiles(list, folder) {
    var byName = {}, maps = [];
    list.forEach(function (x) {
      if (x.rel.indexOf('/') < 0) {
        if (known(x.rel)) byName[x.rel.toLowerCase()] = x.file;
      } else if (/^maps\//i.test(x.rel) && MAP_FILE.test(x.rel)) {
        maps.push({ rel: 'maps/' + x.rel.slice(5), file: x.file });
      }
    });
    var take = NEED.concat(MORE).filter(function (n) { return byName[n.toLowerCase()]; });
    var lack = NEED.filter(function (n) { return !byName[n.toLowerCase()] && !have(n); });
    if (folder && lack.length) {
      pickError('“' + folder + '” has no ' + lack.map(shown).join(', ') + '. Choose the folder StarCraft 1.17 is ' +
                'installed in, the one with StarDat.mpq in it.');
      return;
    }
    if (!take.length) {
      pickError('None of those are the files listed below. Pick them from the folder StarCraft is installed in.');
      return;
    }
    var total = take.reduce(function (s, n) { return s + byName[n.toLowerCase()].size; }, 0), done = 0;
    var chain = Promise.resolve();
    take.forEach(function (n) {
      chain = chain.then(function () {
        say('copying', 'Keeping Your Files', n, done / total);
        return byName[n.toLowerCase()].arrayBuffer().then(function (b) {
          put(n, new Uint8Array(b));
          done += b.byteLength;
        });
      });
    });
    chain = chain.then(function () {
      say('copying', 'Keeping Your Files', 'Maps', 1);
      return Promise.all(maps.map(function (m) {
        return m.file.arrayBuffer().then(function (b) { putUnder(m.rel, new Uint8Array(b)); });
      }));
    });
    var p = readPref();
    p.source = 'own';
    writePref(p);
    chain.then(function () { return sync(false); }).then(function () {
      var still = missing();
      if (!still.length) return start();
      // files picked one by one, some still to pick
      choose(still);
      $('setup-status').hidden = false;
      $('setup-status').textContent = 'Kept ' + take.map(shown).join(', ') + '. Still to pick: ' +
        still.map(shown).join(', ') + '.';
    }).catch(failed);
  }
  function shown(name) { return WHAT[name.toLowerCase()][0]; }
  // the host's own files, as the player chose
  function fromHost() {
    var p = readPref();
    p.source = 'host';
    writePref(p);
    fromAddress(OFFER).then(function () { return sync(false); }).then(start).catch(failed);
  }
  function forget() {
    NEED.concat(MORE).forEach(function (n) {
      var old = have(n);
      if (old) M.FS.unlink(ROOT + '/' + old);
    });
    var p = readPref();
    delete p.source;
    writePref(p);
    sync(false).then(function () { choose(missing()); }).catch(failed);
  }

  // a line for this page's log and, in the room's page, the room's
  function note(text) {
    line('err', text);
    if (!EMBED || window.parent === window) return;
    try { window.parent.postMessage({ hy: 'note', text: String(text) }, location.origin); } catch (e) { /* gone */ }
  }
  // the tab hidden or shown while the game runs: a hidden tab's timers are slowed by the browser
  document.addEventListener('visibilitychange', function () {
    if (window.HyPlay.state === 'running') note('the tab is ' + document.visibilityState);
  });

  // ---- the browser's settings, to a file and back ------------------------------------------------
  var GAME_SETTINGS = ROOT + '/Data/settings.json';
  function note(text, bad) {
    var n = $('settings-note');
    n.hidden = false;
    n.textContent = text;
    n.classList.toggle('hp-bad', !!bad);
  }
  function saveSettings() {
    var out = { kind: 'hyperion-browser-settings', version: 1, saved: new Date().toISOString(), page: readPref(), game: null };
    try { out.game = M.FS.readFile(GAME_SETTINGS, { encoding: 'utf8' }); } catch (e) { /* none yet */ }
    var a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([JSON.stringify(out, null, 2)], { type: 'application/json' }));
    a.download = 'hyperion-browser-settings.json';
    document.body.appendChild(a);
    a.click();
    setTimeout(function () { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
    note('Saved: where the files come from' + (out.game ? ', and the game’s settings.' : '.'));
  }
  function loadSettings(file) {
    if (!file) return;
    file.text().then(function (t) {
      var s = JSON.parse(t);
      if (!s || s.kind !== 'hyperion-browser-settings') throw new Error('not a settings file');
      writePref(s.page && typeof s.page === 'object' ? s.page : {});
      if (typeof s.game === 'string') {
        try { M.FS.mkdir(ROOT + '/Data'); } catch (e) { /* there already */ }
        M.FS.writeFile(GAME_SETTINGS, s.game);
      }
      return sync(false).then(function () {
        note('Loaded: where the files come from' + (typeof s.game === 'string' ? ', and the game’s settings.' : '.'));
      });
    }).catch(function (e) {
      note('That file could not be loaded: ' + ((e && e.message) || e) + '.', true);
    });
  }
  function fromAddress(base) {
    if (base.charAt(base.length - 1) !== '/') base += '/';
    var names = NEED.filter(function (n) { return !have(n); }), i = 0;
    // and what the address lists in its files.json, by place under the program's folder
    // (its maps); an address without one has only the game's files
    function more() {
      return fetch(base + 'files.json').then(function (r) { return r.ok ? r.json() : []; }, function () { return []; })
        .then(function (list) {
          var k = 0;
          function one() {
            if (k >= list.length) return sync(false);
            var rel = String(list[k++]);
            try { M.FS.stat(ROOT + '/' + rel); return one(); } catch (e) { /* not kept yet */ }
            say('fetching', 'Fetching the Game’s Files', rel, k / list.length);
            return fetch(base + rel.split('/').map(encodeURIComponent).join('/')).then(function (r) {
              if (!r.ok) throw new Error(rel + ': ' + r.status);
              return r.arrayBuffer();
            }).then(function (b) { putUnder(rel, new Uint8Array(b)); return one(); });
          }
          return one();
        });
    }
    function next() {
      if (i >= names.length) return more();
      var n = names[i++];
      say('fetching', 'Fetching the Game’s Files', n, (i - 1) / names.length);
      return fetch(base + encodeURIComponent(n)).then(function (r) {
        if (!r.ok) throw new Error(n + ': ' + r.status);
        return r.arrayBuffer();
      }).then(function (b) { put(n, new Uint8Array(b)); return next(); });
    }
    return next();
  }

  // ---- the program ----------------------------------------------------------------------------
  function fit() {
    var c = $('canvas');
    if (!c.width || !c.height) return;
    var k = Math.min(window.innerWidth / c.width, window.innerHeight / c.height);
    c.style.width = Math.floor(c.width * k) + 'px';
    c.style.height = Math.floor(c.height * k) + 'px';
  }
  // F11 is the browser's full screen. The game would take it as its own hotkey (follow the
  // selected unit) and keep it from the browser, so it goes past the game to the browser
  ['keydown', 'keyup'].forEach(function (t) {
    window.addEventListener(t, function (e) { if (e.key === 'F11') e.stopImmediatePropagation(); }, true);
  });
  // In full screen (F11, or the page's own) the mouse stays in the game while the tab has the
  // focus, as on a PC: the page holds the pointer lock on the canvas, the browser hides its
  // pointer and gives the movement alone, SDL moves the game's cursor by it, and the program keeps
  // the cursor inside the window (port_winmsg.cpp's WinMsgWebHeld), so the view scrolls at the
  // screen's edges. Esc or leaving the tab lets the mouse go, as the browser has it; a click in
  // the game holds it again. In the room's page the frame's top window is the one full screen
  function topWin() { try { return window.top.document ? window.top : window; } catch (e) { return window; } }
  function fullScreen() {
    var w = topWin();
    if (document.fullscreenElement || w.document.fullscreenElement) return true;
    if (w.matchMedia && w.matchMedia('(display-mode: fullscreen)').matches) return true;
    return Math.abs(w.innerWidth - screen.width) <= 1 && Math.abs(w.innerHeight - screen.height) <= 1;
  }
  function holdMouse() {
    var c = $('canvas');
    if (!started || c.hidden || !fullScreen() || !document.hasFocus()) return;
    if (document.pointerLockElement === c || !c.requestPointerLock) return;
    try { var p = c.requestPointerLock(); if (p && p.catch) p.catch(function () {}); } catch (e) { /* not allowed now */ }
  }
  function freeMouse(always) {
    if (document.pointerLockElement && document.exitPointerLock && (always || !fullScreen())) document.exitPointerLock();
  }
  function screenChanged() { if (fullScreen()) holdMouse(); else freeMouse(); }
  $('canvas').addEventListener('mousedown', holdMouse, true);
  window.addEventListener('focus', holdMouse);
  window.addEventListener('resize', screenChanged);
  document.addEventListener('fullscreenchange', screenChanged);
  if (topWin() !== window) {
    topWin().addEventListener('resize', screenChanged);
    topWin().document.addEventListener('fullscreenchange', screenChanged);
  }
  var started = false;
  function start() {
    var lack = missing();
    if (lack.length) { choose(lack); return; }
    if (started) return;
    started = true;
    say('running', 'Starting Hyperion', '', 1);
    $('loading').hidden = true;
    $('canvas').hidden = false;
    document.body.classList.add('hp-playing');
    new ResizeObserver(fit).observe($('canvas'));
    window.addEventListener('resize', fit);
    // what the program writes into its data folder is kept: every 30 s, and as the page goes
    setInterval(function () { sync(false).catch(function () {}); }, 30000);
    window.addEventListener('pagehide', function () { sync(false).catch(function () {}); });
    $('canvas').focus();
    // ?args= as words, or as a JSON list for arguments with spaces in them
    var a = Q.get('args') || '';
    var args = a.charAt(0) === '[' ? JSON.parse(a) : a.split(' ').filter(Boolean);
    window.HyPlay.state = 'running';
    tell('running', 'Hyperion', '', 1);
    holdMouse();
    try { M.callMain(args); } catch (e) { if (e !== 'unwind' && !(e && e.name === 'ExitStatus')) failed(e); }
  }
  function choose(lack) {
    window.HyPlay.state = 'choose';
    $('loading').hidden = true;
    $('setup').hidden = false;
    $('use-host').hidden = !OFFER;
    $('setup-host').hidden = !OFFER;
    $('setup-error').hidden = true;
    $('setup-status').hidden = true;
    $('setup-text').textContent = 'Hyperion plays from a copy of StarCraft 1.17’s files kept in this browser. ' +
      (OFFER ? 'The quickest is the host’s copy. Or use your own: choose the folder StarCraft is installed in on this ' +
               'device, or pick the files listed below one by one.'
             : 'Choose the folder StarCraft is installed in on this device, or pick the files listed below one by one.');
    $('pick-folder-btn').classList.toggle('gold', !OFFER && lack.length > 0);
    $('setup-folder-note').textContent = 'It is usually C:\\Program Files (x86)\\StarCraft. ' +
      (window.showDirectoryPicker
        ? 'Your browser asks to let this page view the folder’s files: '
        : 'Your browser then asks whether to “upload” the folder’s files to this site. That is its word for letting ' +
          'this page read them: ') +
      'the page reads only the files below and the maps, and keeps a copy in this browser’s own storage on this ' +
      'device. Nothing is sent to the host or anywhere else.';
    var list = $('setup-list');
    list.textContent = '';
    NEED.concat(MORE).forEach(function (n) {
      var w = WHAT[n.toLowerCase()], kept = !!have(n), need = NEED.indexOf(n) >= 0;
      var li = document.createElement('li'), b = document.createElement('b'), what = document.createElement('span'),
          mark = document.createElement('span');
      b.textContent = w[0];
      what.className = 'hp-what';
      what.textContent = w[1];
      mark.className = 'hp-mark' + (kept ? ' kept' : need ? ' need' : '');
      mark.textContent = kept ? 'Kept' : need ? 'Needed' : 'Optional';
      li.appendChild(b);
      li.appendChild(what);
      li.appendChild(mark);
      list.appendChild(li);
    });
    $('setup-kept').hidden = lack.length > 0;
    var p = readPref();
    $('setup-kept-text').textContent = 'The files are kept in this browser' +
      (p.source === 'host' ? ', from the host.' : p.source === 'own' ? ', from your folder.' : '.');
    tell('choose', 'Your StarCraft Files', lack.length ? 'Missing: ' + lack.join(', ') : '', 0);
  }
  function failed(e) {
    line('err', String((e && e.stack) || e));
    window.HyPlay.state = 'failed';
    freeMouse(true);
    $('canvas').hidden = true;
    document.body.classList.remove('hp-playing');
    say('failed', 'Hyperion Stopped', String((e && e.message) || e), 0);
  }

  // The browser's own zoom kept off the game: a pinch on a touchpad, which comes to the page as
  // the wheel with Ctrl, the wheel with Ctrl itself, and Ctrl with + - 0, all in reach of a player
  // holding Ctrl for the control groups. Chrome zooms the page under the frame by them, so the
  // picture showed one corner of the game blown up. The game still gets the wheel and the keys.
  window.addEventListener('wheel', function (e) { if (e.ctrlKey) e.preventDefault(); }, { passive: false });
  window.addEventListener('keydown', function (e) {
    if ((e.ctrlKey || e.metaKey) && /^(=|\+|-|_|0)$/.test(e.key)) e.preventDefault();
  }, true);
  ['gesturestart', 'gesturechange'].forEach(function (t) {
    document.addEventListener(t, function (e) { e.preventDefault(); }, { passive: false });
  });

  $('pick-folder-btn').addEventListener('click', pickFolder);
  // a folder input gives each file its path with the folder's own name first
  $('pick-folder').addEventListener('change', function (e) {
    var f = [].slice.call(e.target.files);
    e.target.value = '';
    if (!f.length) return;
    takeFiles(f.map(function (x) { return { rel: x.webkitRelativePath.split('/').slice(1).join('/'), file: x }; }),
              f[0].webkitRelativePath.split('/')[0]);
  });
  $('pick-files').addEventListener('change', function (e) {
    var f = [].slice.call(e.target.files);
    e.target.value = '';
    if (f.length) takeFiles(f.map(function (x) { return { rel: x.name, file: x }; }), '');
  });
  $('use-host').addEventListener('click', fromHost);
  $('play-now').addEventListener('click', start);
  $('forget').addEventListener('click', forget);
  $('save-settings').addEventListener('click', saveSettings);
  $('load-settings').addEventListener('change', function (e) { loadSettings(e.target.files[0]); e.target.value = ''; });

  // The program's TCP connections, which Emscripten makes WebSockets to ws://<address>:<port>/,
  // go to the room's bridge for its game port instead (/tcp/<port>, port_lanweb.cpp): the room
  // the page came from, or ?room=<address:port> of its page.
  var ROOM = Q.get('room') || (Q.get('files') ? '' : location.host);
  if (ROOM) {
    var Native = window.WebSocket;
    var Bridged = function (url, protocols) {
      var m = /^wss?:\/\/[^\/:]+:(\d+)\/?$/.exec(String(url));
      if (m) url = (location.protocol === 'https:' ? 'wss://' : 'ws://') + ROOM + '/tcp/' + m[1];
      var ws = protocols === undefined ? new Native(url) : new Native(url, protocols), opened = 0;
      // how the game's connection ended, in this page's log and, through the room's page, the
      // room's (hyperion_lan.log), since a browser's own is out of the host's reach
      if (m) ws.addEventListener('open', function () { opened = Date.now(); });
      if (m) ws.addEventListener('close', function (e) {
        note('a game connection (port ' + m[1] + ') ' + (opened ? 'closed after ' + ((Date.now() - opened) / 1000).toFixed(1) + ' s' : 'did not open') +
             ': code ' + e.code + (e.reason ? ' ' + e.reason : '') + (e.wasClean ? '' : ', not cleanly') +
             (document.hidden ? ', the tab hidden' : '') + (navigator.onLine === false ? ', the browser offline' : ''));
      });
      return ws;
    };
    Bridged.prototype = Native.prototype;
    ['CONNECTING', 'OPEN', 'CLOSING', 'CLOSED'].forEach(function (k) { Bridged[k] = Native[k]; });
    window.WebSocket = Bridged;
  }

  say('loading', 'Starting', 'Loading Hyperion', 0);
  // ?env= as NAME=value,NAME=value, or as a JSON object for values with commas in them
  var env = {}, e = Q.get('env') || '';
  if (e.charAt(0) === '{') env = JSON.parse(e);
  else e.split(',').forEach(function (kv) {
    var i = kv.indexOf('=');
    if (i > 0) env[kv.slice(0, i)] = kv.slice(i + 1);
  });
  HyperionModule({
    canvas: $('canvas'),
    // SDL3's two pointer helpers, which SDL defines only where they are missing: its own,
    // for 64-bit memory, take the pointers Emscripten gives JavaScript for BigInts, and
    // Emscripten gives numbers. A pointer going back into the program, as SDL's canvas
    // handlers pass their window to it, is a BigInt all the same: a number there threw, so
    // no movement or click of the mouse on the canvas reached the game
    SDL3: {
      CPtrToHeap32Index: function (p) { return Math.floor(Number(p) / 4); },
      JSVarToCPtr: function (v) { return typeof v === 'bigint' ? v : BigInt(v); }
    },
    // a seat of a LAN room waiting for its match, or gone into a game (port_front.cpp's SeatWait)
    hySay: function (where) {
      if (!EMBED || window.parent === window) return;
      try { window.parent.postMessage({ hy: 'game', where: String(where) }, location.origin); } catch (e) { /* gone */ }
    },
    print: function (t) { line('out', t); },
    printErr: function (t) { line('err', t); },
    preRun: [function (m) { Object.keys(env).forEach(function (k) { m.ENV[k] = env[k]; }); }],
    onAbort: function (what) { failed(new Error('aborted: ' + what)); },
    // the program is ending (Exit Program; PortWebExit): in the room's page, back to it, where the
    // room starts the seat's Hyperion again for the next match; alone, this page again. What the
    // program wrote is kept first
    hyExit: function () {
      window.HyPlay.state = 'exited';
      freeMouse(true);
      sync(false).catch(function () {}).then(function () {
        if (EMBED && window.parent !== window) {
          try { window.parent.postMessage({ hy: 'exit' }, location.origin); } catch (e) { /* gone */ }
        } else {
          location.reload();
        }
      });
    }
  }).then(function (m) {
    M = m;
    M.FS.mkdir(ROOT);
    M.FS.mount(M.IDBFS, {}, ROOT);
    return sync(true);
  }).then(function () {
    if (Q.get('files')) return fromAddress(Q.get('files')).then(start);
    var lack = missing();
    // the setup asked for, or the files to choose; the host's again when the player chose them
    if (Q.get('setup') === '1') return choose(lack);
    if (!lack.length) return start();
    if (OFFER && readPref().source === 'host') return fromAddress(OFFER).then(function () { return sync(false); }).then(start);
    choose(lack);
  }).catch(failed);
})();
