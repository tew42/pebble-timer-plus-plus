// Exercise the configuration page's custom code the way Clay actually runs it.
//
// index.js hands the function to new Clay(); Clay stringifies it into the configuration page as
// `window.customFn = <source>` and calls it there. Nothing else from index.js comes with it, so
// evaluating the body with this module's variables in scope tests an algorithm the page never
// runs. Take the whole function and eval it with only what the page provides.
var fs = require('fs');
var ROOT = process.argv[2] || require('path').resolve(__dirname, '..', '..');
var cfg = JSON.parse(fs.readFileSync(ROOT + '/src/pkjs/config.json'));
var NEVER = 255;   // the test's own copy; the page's must come from inside the function
var SEC_IN_MIN = 60;

var failures = 0;
function fail(msg) { console.log('  FAIL: ' + msg); failures++; }

// index.js answers the watch when it asks for the settings. That path never runs on the
// configuration page, so unlike the custom function it can be loaded as the phone loads it --
// with the real Clay, which is what turns the stored settings into an AppMessage. Clay wants
// `message_keys`, a module only the phone's runtime has, so the resolver is taught about it.
function loadIndexJs(stored) {
    var Module = require('module');
    var path = require('path');
    var KEYS = {tenSecondUpdatesAbove: 10001, minuteUpdatesAbove: 10002,
                timerColor: 10003, chronoColor: 10004, settingsRequest: 10005,
                instantStart: 10006, overrunChrono: 10007};
    // message_keys is the phone runtime's; the page HTML is written by the Pebble build. Both
    // are stood in for so the rest of Clay is the real thing.
    // message_keys is the phone runtime's, the page HTML is written by the Pebble build, and
    // Clay's own dependencies are installed by that build rather than shipped. None of them is
    // on the path this exercises -- they belong to generating the page -- so they are stood in
    // for and the rest of Clay, prepareSettingsForAppMessage above all, is the real thing.
    var STANDINS = {
        'message_keys': KEYS,
        './tmp/config-page.html': '$$RETURN_TO$$$$CUSTOM_FN$$$$CONFIG$$$$SETTINGS$$' +
                                  '$$COMPONENTS$$$$META$$',
        'tosource': function(x) { return JSON.stringify(x); },
        'deepcopy/build/deepcopy.min': function(x) { return JSON.parse(JSON.stringify(x)); },
        './src/scripts/components': []
    };
    var resolve = Module._resolveFilename;
    Module._resolveFilename = function(request) {
        if (STANDINS.hasOwnProperty(request)) { return request; }
        return resolve.apply(this, arguments);
    };
    Object.keys(STANDINS).forEach(function(name) {
        require.cache[name] = new Module(name);
        require.cache[name].exports = STANDINS[name];
        require.cache[name].loaded = true;
    });

    var listeners = {};
    var sent = [];
    global.Pebble = {
        addEventListener: function(name, fn) { listeners[name] = fn; },
        sendAppMessage: function(dict, ok, err) { sent.push(dict); if (ok) { ok(); } },
        openURL: function() {},
        platform: 'basalt'
    };
    global.localStorage = {
        _v: stored === null ? null : JSON.stringify(stored),
        getItem: function() { return this._v; },
        setItem: function(k, v) { this._v = v; }
    };
    global.navigator = {language: 'en'};
    delete require.cache[require.resolve(ROOT + '/src/pkjs/index.js')];
    delete require.cache[require.resolve(ROOT + '/node_modules/@rebble/clay/index.js')];
    require(ROOT + '/src/pkjs/index.js');
    Module._resolveFilename = resolve;
    return {listeners: listeners, sent: sent, keys: KEYS};
}

console.log('\nthe phone answers the watch when it asks for the settings:');
try {
    var saved = {tenSecondUpdatesAbove: 30, minuteUpdatesAbove: 2,
                 timerColor: 0x00ff00, chronoColor: 0xff0000, instantStart: 10,
                 overrunChrono: false};
    var app = loadIndexJs(saved);
    if (!app.listeners.appmessage) {
        fail('index.js registers no appmessage listener, so the watch is never answered');
    } else {
        app.listeners.appmessage({payload: {}});
        if (app.sent.length !== 1) {
            fail('expected one reply, got ' + app.sent.length);
        } else {
            var dict = app.sent[0];
            Object.keys(saved).forEach(function(key) {
                var id = app.keys[key];
                // a toggle goes over as the integer the watch reads, 1 or 0
                var want = (typeof saved[key] === 'boolean') ? (saved[key] ? 1 : 0) : saved[key];
                if (dict[id] !== want) {
                    fail(key + ' came back as ' + dict[id] + ', expected ' + want);
                }
            });
            console.log('  ok: the six saved settings go back under their own message keys');
        }
    }
    // and with nothing ever saved the phone stays quiet, so the watch keeps its own defaults
    var fresh = loadIndexJs(null);
    fresh.listeners.appmessage({payload: {}});
    if (fresh.sent.length !== 0) {
        fail('the phone answered with ' + JSON.stringify(fresh.sent[0]) + ' before anything was saved');
    } else {
        console.log('  ok: nothing saved yet, nothing sent, and the watch keeps its defaults');
    }
} catch (e) {
    fail('could not load index.js the way the phone does: ' + e);
}

function itemFor(key) {
    var found = null;
    (function walk(items) {
        items.forEach(function(it) {
            if (it.messageKey === key) { found = it; }
            if (it.items) { walk(it.items); }
        });
    })(cfg);
    return found;
}
// the overrun toggle defaults on, the behavior the app has always had
(function () {
    var item = itemFor('overrunChrono');
    if (!item || item.type !== 'toggle' || item.defaultValue !== true) {
        fail('the overrun toggle is missing or does not default on');
    }
})();

function optionsFor(key) {
    return itemFor(key).options.map(function(o) { return parseInt(o.value, 10); });
}
var TENS = optionsFor('tenSecondUpdatesAbove');
var MINS = optionsFor('minuteUpdatesAbove');
console.log('config.json options: 10s', TENS.join(','), '| min', MINS.join(','));

// Instant start is a window and a credit cap in one number, so the page may only offer values
// settings.c will take: Off, or between its floor and its ceiling. An option outside that is
// silently refused on arrival, leaving a setting the page offers and the watch ignores.
(function () {
    var src = fs.readFileSync(ROOT + '/src/c/settings.h', 'utf8');
    function bound(name) {
        var m = new RegExp('#define ' + name + ' (\\d+)').exec(src);
        if (!m) { fail('settings.h has no ' + name); return null; }
        return parseInt(m[1], 10);
    }
    var never = bound('SETTINGS_NEVER');
    var lo = bound('SETTINGS_INSTANT_START_MIN_SEC');
    var hi = bound('SETTINGS_INSTANT_START_MAX_SEC');
    var opts = optionsFor('instantStart');
    if (opts.indexOf(never) < 0) {
        fail('the instant start select offers no Off option');
    }
    opts.forEach(function (v) {
        if (v !== never && (v < lo || v > hi)) {
            fail('the page offers instant start ' + v + 's, outside settings.h\'s ' + lo + '-' + hi);
        }
    });
    var item = itemFor('instantStart');
    if (parseInt(item.defaultValue, 10) !== never) {
        fail('instant start defaults to ' + item.defaultValue + ', not Off');
    }
    console.log('instant start options: ' + opts.join(',') +
                ' (Off=' + never + ', watch accepts ' + lo + '-' + hi + '), default Off');
})();

// The color map from the SDK's Color Picker Tool page in the Rebble developer docs, as each
// hexagon's half-column on its row, top row first: what both pickers are laid out to follow.
var SDK_MAP = [
    [[5, 'aaff55'], [11, 'ffffaa'], [15, 'aaaaaa'], [17, 'ffffff']],
    [[0, 'aaffaa'], [4, '55ff00'], [6, 'aaff00'], [12, 'ffff55'], [16, '000000'], [18, '555555']],
    [[1, '55ff55'], [3, '00ff00'], [11, 'ffff00'], [13, 'ffaa55']],
    [[2, '00ff55'], [4, '00aa00'], [6, '55aa00'], [8, 'aaaa55'], [10, 'aaaa00'], [12, 'ffaa00'], [14, 'ff5500'], [18, 'ffaaaa']],
    [[1, '55ffaa'], [3, '00aa55'], [5, '55aa55'], [7, '005500'], [9, '555500'], [13, 'aa5500'], [15, 'ff0000'], [17, 'ff5555']],
    [[2, '00ffaa'], [4, '00aaaa'], [6, '55aaaa'], [8, '005555'], [14, 'aa5555'], [16, 'ff0055']],
    [[1, '55ffff'], [3, '00ffff'], [7, '0055aa'], [13, '550000'], [15, 'aa0000']],
    [[0, 'aaffff'], [6, '00aaff'], [8, '0000aa'], [10, '000055'], [12, '550055'], [14, 'aa0055'], [16, 'ff00aa']],
    [[5, '55aaff'], [7, '0000ff'], [9, '5500ff'], [11, '5500aa'], [13, 'aa00aa'], [15, 'ff00ff'], [17, 'ff55aa']],
    [[6, '0055ff'], [8, '5555ff'], [10, '5555aa'], [12, 'aa00ff'], [14, 'aa55aa'], [16, 'ff55ff'], [18, 'ffaaff']],
    [[9, 'aaaaff'], [13, 'aa55ff']]
];
var MAP_COLUMNS = 20;

// The palettes' rules. Coarse mode offers the 24 accents palettes.json pairs with bands and
// moves every other color as its table says; without it anything goes but black and white, which
// become the default green. The table is gen_palette.js's, run again here so it cannot drift from
// the rule it was worked out by; settingstest.c holds the watch to the same table.
var gen = require('./gen_palette.js');
var PALETTES = JSON.parse(fs.readFileSync(require('path').join(__dirname, 'palettes.json')));
function offered(hex, coarse) {
    return coarse ? PALETTES.coarse[hex] === hex : hex !== '000000' && hex !== 'ffffff';
}
function movedTo(hex, coarse) {
    if (coarse) { return PALETTES.coarse[hex]; }
    return offered(hex, false) ? hex : gen.DEFAULT;
}
var ALL64 = gen.ALL;

(function () {
    if (JSON.stringify(PALETTES.pairs) !== JSON.stringify(gen.PAIRS) ||
        JSON.stringify(PALETTES.coarse) !== JSON.stringify(gen.moves())) {
        fail('palettes.json is not what gen_palette.js works out; run node gen_palette.js');
    }
    // every color moves onto one of the 24, and each of the 24 stays where it is
    var accents = PALETTES.pairs.map(function(pair) { return pair[0]; });
    ALL64.forEach(function(hex) {
        if (accents.indexOf(PALETTES.coarse[hex]) < 0) {
            fail('coarse mode moves ' + hex + ' to ' + PALETTES.coarse[hex] + ', which it does not offer');
        }
    });
    if (ALL64.filter(function(hex) { return offered(hex, true); }).length !== 24) {
        fail('coarse mode does not leave exactly its 24 accents where they are');
    }
    var layouts = ['timerColor', 'chronoColor'].map(function(key) { return itemFor(key).layout; });
    if (JSON.stringify(layouts[0]) !== JSON.stringify(layouts[1])) {
        fail('the two pickers are laid out differently');
    }
    var layout = layouts[0] || [];
    if (layout.length !== SDK_MAP.length || layout.some(function(row) {
        return row.length !== MAP_COLUMNS;
    })) {
        fail('the layout is not the map\'s ' + SDK_MAP.length + ' rows of ' + MAP_COLUMNS);
        return;
    }
    // every hexagon where the map has it, offered if some palette offers it, and nothing anywhere
    // else
    var expected = SDK_MAP.map(function() {
        var row = []; for (var i = 0; i < MAP_COLUMNS; i++) { row.push(false); } return row;
    });
    SDK_MAP.forEach(function(row, r) {
        row.forEach(function(cell) {
            var hex = cell[1];
            expected[r][cell[0]] = (offered(hex, false) || offered(hex, true)) ? hex : false;
        });
    });
    var placed = 0, coarse = 0;
    layout.forEach(function(row, r) {
        row.forEach(function(value, c) {
            if (value !== expected[r][c]) {
                fail('row ' + r + ', half-column ' + c + ' holds ' + value + ', the map has ' +
                     expected[r][c]);
            }
            if (value) {
                placed++;
                coarse += offered(value, true) ? 1 : 0;
            }
        });
    });
    if (placed !== 62 || coarse !== 24) {
        fail('placed ' + placed + ' colors, ' + coarse + ' for coarse mode; expected 62 and 24');
    }
    ['timerColor', 'chronoColor'].forEach(function(key) {
        var hex = String(itemFor(key).defaultValue);
        if (!offered(hex, false) || !offered(hex, true)) {
            fail(key + ' defaults to ' + hex + ', which one of the palettes does not offer');
        }
    });
    if (!failures) {
        console.log('both pickers follow the SDK color map: 62 placed, 24 in coarse mode, ' +
                    'defaults in both, the table as gen_palette.js works it out');
    }
})();

// Lift the custom function out of index.js exactly as Clay would
function customFunction() {
    var src = fs.readFileSync(ROOT + '/src/pkjs/index.js', 'utf8');
    var start = src.indexOf('function() {', src.indexOf('new Clay('));
    if (start < 0) { throw new Error('no custom function passed to new Clay()'); }
    var depth = 0, end = -1;
    for (var i = src.indexOf('{', start); i < src.length; i++) {
        if (src[i] === '{') { depth++; }
        if (src[i] === '}') { depth--; if (depth === 0) { end = i + 1; break; } }
    }
    // `window.customFn = <source>` in the configuration page. new Function() compiles in global
    // scope, so the function sees no more than it would there -- a direct eval() here would hand
    // it this file's variables and hide exactly the bug this is here to catch.
    return new Function('return (' + src.slice(start, end) + ')')();
}

// A stand-in for the page's DOM, built out of the real layout in config.json.
//
// The picker code in the custom function reaches for the document, which Node has not got, so
// the harness has to bring one. It is a stand-in and worth naming as such: it answers only the
// handful of calls that code makes, and a stub written to fit the code it tests can catch that
// code changing but never that it was right to begin with. What established that was a browser:
// the real Clay page, built from this config and this custom function, rendered in Chromium,
// with every hexagon measured in both palettes. This keeps the result from rotting.
//
// It builds what Clay's color component builds: every cell of the layout as a box with its width
// set inline, colors carrying a data-value and gaps carrying none, inside the same wrappers, and
// a head for the stylesheet the hexagons are cut by.
function fakeDom() {
    function matches(node, selector) {
        var ok = true;
        selector.replace(/\.([\w-]+)|\[([\w-]+)\]/g, function(whole, cls, attr) {
            if (cls && (' ' + node.className + ' ').indexOf(' ' + cls + ' ') < 0) { ok = false; }
            if (attr && typeof node.attrs[attr] === 'undefined') { ok = false; }
            return '';
        });
        return ok;
    }
    function descendants(node, out) {
        node.children.forEach(function(child) { out.push(child); descendants(child, out); });
        return out;
    }
    function element(className, attrs) {
        var node = {
            className: className, id: '', title: '', textContent: '', hex: null,
            attrs: attrs || {}, children: [], nextElementSibling: null, style: {},
            classList: {add: function(cls) { node.className += ' ' + cls; }},
            getAttribute: function(name) { return this.attrs[name]; },
            appendChild: function(child) { this.children.push(child); return child; },
            insertBefore: function(child, before) {
                var at = this.children.indexOf(before);
                this.children.splice(at < 0 ? this.children.length : at, 0, child);
                return child;
            },
            querySelectorAll: function(selector) {
                return descendants(this, []).filter(function(n) { return matches(n, selector); });
            },
            querySelector: function(selector) {
                return this.querySelectorAll(selector)[0] || null;
            }
        };
        return node;
    }
    function picker(layout) {
        var root = element('component component-color');
        root.children.push(element('label'));
        var wrapOuter = element('picker-wrap');
        var inner = element('picker');
        var wrap = element('color-box-wrap');
        var container = element('color-box-container');
        var columns = layout[0].length;
        layout.forEach(function(row) {
            row.forEach(function(hex) {
                var box = element('color-box' + (hex ? ' selectable' : ''),
                                  hex ? {'data-value': String(parseInt(hex, 16))} : {});
                box.hex = hex || null;
                box.style.width = (100 / columns) + '%';
                var previous = container.children[container.children.length - 1];
                if (previous) { previous.nextElementSibling = box; }
                container.children.push(box);
            });
        });
        wrap.children.push(container);
        inner.children.push(wrap);
        wrapOuter.children.push(inner);
        root.children.push(wrapOuter);
        return root;
    }
    var pickers = {timerColor: picker(itemFor('timerColor').layout),
                   chronoColor: picker(itemFor('chronoColor').layout)};
    var head = element('head');
    var root = element('root');
    root.children = [head, pickers.timerColor, pickers.chronoColor];
    return {
        document: {
            head: head,
            querySelectorAll: function(selector) { return root.querySelectorAll(selector); },
            getElementById: function(id) {
                return root.querySelectorAll('').filter(function(n) { return n.id === id; })[0] || null;
            },
            createElement: function() { return element(''); }
        },
        window: {},
        pickers: pickers
    };
}

// A stand-in for the built page. Clay's `val` manipulator returns a number when the item sets
// serializeValueAs "integer" and the option string otherwise, and its set() only fires "change"
// when the value really changes -- both worth reproducing, the second because it is what stops
// a correcting handler from recursing forever. A color item's value is always a number, and its
// element is the picker the stand-in DOM built for it.
function buildPage(customFn, opts) {
    var values = {tenSecondUpdatesAbove: opts.ten, minuteUpdatesAbove: opts.min,
                  timerColor: opts.timer === undefined ? 0x00ff00 : opts.timer,
                  chronoColor: opts.chrono === undefined ? 0x00ff00 : opts.chrono};
    var dom = fakeDom();
    var changed = {};
    var afterBuild = [];
    var sets = 0;
    var items = {};
    Object.keys(values).forEach(function(key) {
        var color = key === 'timerColor' || key === 'chronoColor';
        changed[key] = [];
        items[key] = {
            get: function() { return (opts.asNumber || color) ? values[key] : String(values[key]); },
            set: function(value) {
                if (String(value) === String(values[key])) { return; }
                values[key] = parseInt(value, 10);
                if (++sets > 40) { throw new Error('set() looped'); }
                changed[key].forEach(function(handler) { handler(); });
            },
            on: function(event, handler) {
                if (event === 'change') { changed[key].push(handler); }
            },
            $element: color ? [dom.pickers[key]] : undefined
        };
    });
    customFn.call({
        EVENTS: { AFTER_BUILD: 'AFTER_BUILD' },
        getItemByMessageKey: function(key) { return items[key]; },
        on: function(event, handler) { if (event === 'AFTER_BUILD') { afterBuild.push(handler); } }
    });
    // the custom function is compiled in global scope, exactly as the page compiles it, so the
    // document it works on has to be reachable from there rather than passed in. It stays in
    // place for whatever the caller does next, a change of threshold included; done() takes it away
    global.document = dom.document;
    global.window = dom.window;
    afterBuild.forEach(function(handler) { handler(); });
    return { values: values, sets: sets, dom: dom, items: items,
             done: function() { delete global.document; delete global.window; } };
}

var customFn;
try {
    customFn = customFunction();
} catch (e) {
    fail('the custom function could not be evaluated: ' + e.message);
}

if (customFn) {
    [false, true].forEach(function(asNumber) {
        var label = asNumber ? 'get() returning numbers' : 'get() returning strings';
        var corrected = 0;
        TENS.forEach(function(ten) {
            MINS.forEach(function(min) {
                var page;
                try {
                    page = buildPage(customFn, {ten: ten, min: min, asNumber: asNumber});
                    page.done();
                } catch (e) {
                    fail(label + ', 10s>' + ten + ' min>' + min + ': ' + e.name + ': ' + e.message);
                    return;
                }
                var t = page.values.tenSecondUpdatesAbove;
                var m = page.values.minuteUpdatesAbove;
                // the page must never leave a pair where the ten second setting does nothing
                if (t !== NEVER && m !== NEVER && t >= m * SEC_IN_MIN) {
                    fail(label + ': 10s>' + ten + ' min>' + min + ' settled on 10s>' + t +
                         ' min>' + m + ', where ten second updates never apply');
                }
                // it corrects by switching the ten second setting off, never by moving the
                // minute threshold the user just chose
                if (m !== min) {
                    fail(label + ': min>' + min + ' was moved to ' + m);
                }
                var wasRedundant = ten !== NEVER && min !== NEVER && ten >= min * SEC_IN_MIN;
                if (wasRedundant) {
                    corrected++;
                    if (t !== NEVER) {
                        fail(label + ': 10s>' + ten + ' min>' + min + ' does nothing, but the ten '
                             + 'second setting was left at ' + t);
                    }
                } else if (t !== ten) {
                    fail(label + ': 10s>' + ten + ' min>' + min + ' was already fine, but the ten '
                         + 'second setting moved to ' + t);
                }
            });
        });
        if (!failures) {
            console.log('all ' + (TENS.length * MINS.length) + ' pairs settle with ' + label +
                        '; ' + corrected + ' redundant ones switched the 10s setting to Never');
        }
    });
}

// Every swatch is a pointed hexagon where the map puts it, the rows interlocking as the SDK
// page's do, and none carries a label
if (customFn) {
    var shaped = buildPage(customFn, {ten: NEVER, min: NEVER});
    shaped.done();
    var styles = shaped.dom.document.head.children;
    var hexagon = 'clip-path:polygon(50% 0, 100% 25%, 100% 75%, 50% 100%, 0 75%, 0 25%)';
    if (styles.length !== 1 ||
        !['-webkit-' + hexagon, ';' + hexagon].every(function(rule) {
            return styles[0].textContent.indexOf(rule) >= 0;
        }) ||
        !/\.color-box\.selected\{[^}]*radial-gradient/.test(styles[0].textContent) ||
        !/\.color-box\[data-value\]::after\{[^}]*background:#000;[^}]*clip-path:polygon\(evenodd,/
            .test(styles[0].textContent)) {
        fail('expected one stylesheet cutting hexagons, edging them in black and marking the pick');
    }
    ['timerColor', 'chronoColor'].forEach(function(key) {
        var picker = shaped.dom.pickers[key];
        var cells = picker.querySelectorAll('.color-box');
        var boxes = picker.querySelectorAll('.color-box[data-value]');
        var wrap = picker.querySelector('.color-box-wrap');
        if (picker.className.split(' ').indexOf('hex-map') < 0) {
            fail(key + ': the picker is not marked for the hexagon stylesheet');
        }
        if (boxes.length !== 62 || cells.length !== 220 || !wrap) {
            fail(key + ': ' + boxes.length + ' swatches in ' + cells.length + ' cells, expected 62 in 220');
            return;
        }
        // in percent of the picker's width: a hexagon spans two of the 20 half-columns, is
        // 2/sqrt(3) as tall as it is wide, and the 11 rows sit three quarters of that apart, with
        // the last row's points below them
        var hexWidth = 10, hexHeight = hexWidth * 2 / Math.sqrt(3);
        var pickerHeight = parseFloat(wrap.style.paddingBottom);
        var near = function(a, b) { return Math.abs(a - b) < 1e-9; };
        if (!near(pickerHeight, 11 * hexHeight * 3 / 4 + hexHeight / 4)) {
            fail(key + ': rows padded to ' + wrap.style.paddingBottom);
        }
        boxes.forEach(function(box) {
            // a height in % is of the picker's height, a margin in % of its width
            var height = parseFloat(box.style.height) * pickerHeight / 100;
            if (!near(parseFloat(box.style.width), hexWidth) || !near(height, hexHeight) ||
                !near(parseFloat(box.style.marginBottom), -hexHeight / 4)) {
                fail(key + ': swatch ' + box.hex + ' is ' + box.style.width + ' by ' +
                     box.style.height + ', margin ' + box.style.marginBottom);
            }
            var gap = box.nextElementSibling;
            if (!gap || gap.getAttribute('data-value') || gap.style.display !== 'none') {
                fail(key + ': the gap after ' + box.hex + ' is still taking up room');
            }
            if (box.textContent || box.title) {
                fail(key + ': swatch ' + box.hex + ' is labeled "' + (box.textContent || box.title) + '"');
            }
        });
        // and every empty cell is one row's pitch tall, so no float snags on one taller than a row
        cells.filter(function(cell) { return !cell.getAttribute('data-value'); })
            .forEach(function(cell, ii) {
                if (!near(parseFloat(cell.style.height) * pickerHeight / 100, hexHeight * 3 / 4)) {
                    fail(key + ': empty cell ' + ii + ' is ' + cell.style.height + ' tall');
                }
            });
        if (picker.querySelectorAll('.description').length) {
            fail(key + ': the picker carries a readout');
        }
    });
    if (!failures) {
        console.log('every swatch is an unlabeled hexagon, laid out as the SDK map\'s');
    }

    // each palette offers exactly its colors, the others hidden but keeping their places
    [false, true].forEach(function(coarse) {
        var page = buildPage(customFn, {ten: NEVER, min: coarse ? 5 : NEVER});
        page.done();
        ['timerColor', 'chronoColor'].forEach(function(key) {
            var shown = 0;
            page.dom.pickers[key].querySelectorAll('.color-box[data-value]').forEach(function(box) {
                var visible = box.style.visibility !== 'hidden';
                shown += visible ? 1 : 0;
                if (visible !== offered(box.hex, coarse)) {
                    fail((coarse ? 'coarse' : 'without coarse') + ' ' + (visible ? 'shows ' : 'hides ') + box.hex);
                }
            });
            if (shown !== (coarse ? 24 : 62)) {
                fail((coarse ? 'coarse' : 'without coarse') + ' shows ' + shown + ' in ' + key);
            }
        });
    });
    if (!failures) { console.log('coarse mode shows its 24, otherwise all 62, the rest hidden in place'); }

    // a stored color the palette does not offer is moved on build, as the watch moves it: all 64,
    // in both palettes, and either coarse mode
    var checked = 0;
    [[NEVER, NEVER], [20, NEVER], [NEVER, 5]].forEach(function(pair) {
        var coarse = pair[0] !== NEVER || pair[1] !== NEVER;
        ALL64.forEach(function(hex) {
            var page = buildPage(customFn, {ten: pair[0], min: pair[1], timer: parseInt(hex, 16)});
            page.done();
            var got = ('00000' + page.values.timerColor.toString(16)).slice(-6);
            if (got !== movedTo(hex, coarse)) {
                fail('10s>' + pair[0] + ' min>' + pair[1] + ' kept a stored ' + hex + ' as ' + got +
                     ', the table says ' + movedTo(hex, coarse));
            }
            checked++;
        });
    });
    if (!failures) {
        console.log('a stored color moves into the palette as the watch moves it: ' + checked + ' cases');
    }

    // and turning a coarse mode on does the same to a pick which no longer fits, while turning it
    // off again moves nothing back
    var page = buildPage(customFn, {ten: NEVER, min: NEVER, timer: 0xff0000, chrono: 0x00ff00});
    var hex = function(key) { return ('00000' + page.values[key].toString(16)).slice(-6); };
    if (hex('timerColor') !== 'ff0000') { fail('without coarse mode Red was moved'); }
    page.items.minuteUpdatesAbove.set(5);
    if (hex('timerColor') !== 'ff5555' || hex('chronoColor') !== '00ff00') {
        fail('turning on minute updates left ' + hex('timerColor') + ' and ' + hex('chronoColor') +
             ', expected ff5555 and 00ff00');
    }
    var shownCoarse = page.dom.pickers.timerColor.querySelectorAll('.color-box[data-value]')
        .filter(function(box) { return box.style.visibility !== 'hidden'; });
    if (shownCoarse.length !== 24) { fail('after turning coarse mode on, ' + shownCoarse.length + ' are shown'); }
    page.items.minuteUpdatesAbove.set(NEVER);
    page.items.tenSecondUpdatesAbove.set(20);
    if (hex('timerColor') !== 'ff5555') { fail('ten second updates moved ' + hex('timerColor')); }
    page.items.tenSecondUpdatesAbove.set(NEVER);
    var shownFine = page.dom.pickers.timerColor.querySelectorAll('.color-box[data-value]')
        .filter(function(box) { return box.style.visibility !== 'hidden'; });
    if (hex('timerColor') !== 'ff5555' || shownFine.length !== 62) {
        fail('turning coarse mode off left ' + hex('timerColor') + ' with ' + shownFine.length + ' shown');
    }
    page.done();
    if (!failures) {
        console.log('turning coarse mode on moves a pick that no longer fits; turning it off moves nothing');
    }
}

console.log(failures ? failures + ' FAILURES' : 'configuration page holds');
process.exit(failures ? 1 : 0);
