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
                instantStart: 10006, contrast: 10007};
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
                 timerColor: 0x00ff00, chronoColor: 0xff0000, instantStart: 10, contrast: 1};
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
                if (dict[id] !== saved[key]) {
                    fail(key + ' came back as ' + dict[id] + ', expected ' + saved[key]);
                }
            });
            console.log('  ok: the five saved settings go back under their own message keys');
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

// The two contrast modes' rules, stated here on their own rather than taken from the page: regular
// shades the center two steps up, so it needs a channel at 00; high shades the band two steps
// down, so it needs a channel at ff; and neither can do anything with a color which has no hue.
// Channels are the two-bit levels GColorFromHEX takes from the top of each byte.
function channelsOf(hex) {
    return [0, 2, 4].map(function(i) { return parseInt(hex.substr(i, 2), 16) >> 6; });
}
function drawable(hex, high) {
    var c = channelsOf(hex);
    var lo = Math.min.apply(Math, c), hi = Math.max.apply(Math, c);
    return lo < hi && (high ? hi === 3 : lo === 0);
}
// Where each mode moves a color it cannot draw: one list, which settingstest.c holds the watch to
var MOVES = JSON.parse(fs.readFileSync(require('path').join(__dirname, 'accent_moves.json')));
function movedTo(hex, high) {
    return MOVES[high ? 'high' : 'regular'][hex] || hex;
}
var ALL64 = [];
[0, 0x55, 0xaa, 0xff].forEach(function(r) { [0, 0x55, 0xaa, 0xff].forEach(function(g) {
    [0, 0x55, 0xaa, 0xff].forEach(function(b) {
        ALL64.push(('00000' + (r << 16 | g << 8 | b).toString(16)).slice(-6));
    });
}); });

(function () {
    // the list and the rules agree: a mode moves exactly the colors it cannot draw, onto ones it can
    ALL64.forEach(function(hex) {
        [false, true].forEach(function(high) {
            var mode = high ? 'high' : 'regular';
            if (drawable(hex, high) === (hex in MOVES[mode])) {
                fail('accent_moves.json ' + (drawable(hex, high) ? 'moves ' : 'leaves ') + hex +
                     ', which ' + mode + ' ' + (drawable(hex, high) ? 'can' : 'cannot') + ' draw');
            }
            if (!drawable(movedTo(hex, high), high)) {
                fail(mode + ' moves ' + hex + ' to ' + movedTo(hex, high) + ', which it cannot draw');
            }
        });
    });
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
    // every hexagon where the map has it, offered if some mode can draw it, and nothing anywhere else
    var expected = SDK_MAP.map(function() {
        var row = []; for (var i = 0; i < MAP_COLUMNS; i++) { row.push(false); } return row;
    });
    SDK_MAP.forEach(function(row, r) {
        row.forEach(function(cell) {
            var hex = cell[1];
            expected[r][cell[0]] = (drawable(hex, false) || drawable(hex, true)) ? hex : false;
        });
    });
    var placed = 0, regular = 0, high = 0;
    layout.forEach(function(row, r) {
        row.forEach(function(value, c) {
            if (value !== expected[r][c]) {
                fail('row ' + r + ', half-column ' + c + ' holds ' + value + ', the map has ' +
                     expected[r][c]);
            }
            if (value) {
                placed++;
                regular += drawable(value, false) ? 1 : 0;
                high += drawable(value, true) ? 1 : 0;
            }
        });
    });
    if (placed !== 54 || regular !== 36 || high !== 36) {
        fail('placed ' + placed + ' colors, ' + regular + ' for regular and ' + high +
             ' for high; expected 54, 36 and 36');
    }
    ['timerColor', 'chronoColor'].forEach(function(key) {
        var hex = String(itemFor(key).defaultValue);
        if (!drawable(hex, false) || !drawable(hex, true)) {
            fail(key + ' defaults to ' + hex + ', which one of the modes cannot draw');
        }
    });
    if (!failures) {
        console.log('both pickers follow the SDK color map: 54 placed, 36 drawable in each mode, ' +
                    'defaults in both');
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
// with every swatch measured against its own cell in both modes. This keeps the result from
// rotting.
//
// It builds what Clay's color component builds: every cell of the layout as a box with its width
// set inline, colors carrying a data-value and gaps carrying none, inside the same wrappers. The
// backgrounds it reports are the uncorrected hex, which is what Clay paints while the color items
// set "sunlight": false. Turn that on and the page shades the swatches through Clay's sunlight
// map, and this would have to learn it before the lettering test meant anything.
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
        return {
            className: className, title: '', textContent: '', hex: null,
            attrs: attrs || {}, children: [], nextElementSibling: null, style: {cssText: ''},
            getAttribute: function(name) { return this.attrs[name]; },
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
    var root = element('root');
    root.children = [pickers.timerColor, pickers.chronoColor];
    return {
        document: {
            querySelectorAll: function(selector) { return root.querySelectorAll(selector); },
            createElement: function() { return element(''); }
        },
        window: {
            getComputedStyle: function(node) {
                var channels = [0, 2, 4].map(function(i) {
                    return parseInt((node.hex || '000000').substr(i, 2), 16);
                });
                return {backgroundColor: 'rgb(' + channels.join(', ') + ')'};
            }
        },
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
                  contrast: opts.contrast || 0,
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
    // place for whatever the caller does next, a switch of contrast included; done() takes it away
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

// Every swatch says which color it is and sits where the map puts it, because a good many of the
// accents on offer differ by a single two-bit channel and a phone screen does not make that plain.
if (customFn) {
    var shaped = buildPage(customFn, {ten: NEVER, min: NEVER});
    shaped.done();
    ['timerColor', 'chronoColor'].forEach(function(key) {
        var picker = shaped.dom.pickers[key];
        var cells = picker.querySelectorAll('.color-box');
        var boxes = picker.querySelectorAll('.color-box[data-value]');
        var inks = {};
        if (boxes.length !== 54) { fail(key + ': ' + boxes.length + ' swatches, expected 54'); }
        boxes.forEach(function(box) {
            // the label is the swatch's own hex, recovered from the decimal Clay writes on it, so a
            // dropped leading zero shows up here rather than on the phone
            if (box.textContent !== box.hex) {
                fail(key + ': swatch ' + box.hex + ' is labelled "' + box.textContent + '"');
            }
            if (!box.title || box.title === box.hex) {
                fail(key + ': swatch ' + box.hex + ' is offered but has no name');
            }
            var ink = /color:(#[0-9a-f]{3,6})/.exec(box.style.cssText);
            if (!ink) {
                fail(key + ': swatch ' + box.hex + ' got no lettering color');
            } else {
                inks[ink[1]] = (inks[ink[1]] || 0) + 1;
                // the two unarguable ones: yellow cannot carry white, blue cannot carry black
                if (box.hex === 'ffff00' && ink[1] !== '#000') { fail(key + ': yellow is lettered ' + ink[1]); }
                if (box.hex === '0000ff' && ink[1] !== '#fff') { fail(key + ': blue is lettered ' + ink[1]); }
            }
            // a hexagon is two half-columns: the swatch spans its own cell and the gap after it
            if (box.style.cssText.indexOf(';width:10%') < 0) {
                fail(key + ': swatch ' + box.hex + ' was not widened over its gap');
            }
            var gap = box.nextElementSibling;
            if (!gap || gap.getAttribute('data-value') || gap.style.display !== 'none') {
                fail(key + ': the gap after ' + box.hex + ' is still taking up room');
            }
        });
        if (Object.keys(inks).length < 2) {
            fail(key + ': every swatch was lettered alike, so the lettering does not follow it');
        }
        // the 11 rows of 20 half-columns, with swatches four fifths as tall as they are wide
        var wrap = picker.querySelector('.color-box-wrap');
        var want = (11 * 2 * 0.8 / 20 * 100) + '%';
        if (cells.length !== 220 || !wrap || wrap.style.paddingBottom !== want) {
            fail(key + ': ' + cells.length + ' cells and rows padded to ' +
                 (wrap && wrap.style.paddingBottom) + ', expected 220 and ' + want);
        }
    });
    if (!failures) {
        console.log('every swatch carries its hex and its name, laid out as the SDK map\'s hexagons');
    }

    // each mode offers exactly the colors it can draw, the others hidden but keeping their places
    [false, true].forEach(function(high) {
        var page = buildPage(customFn, {ten: NEVER, min: NEVER, contrast: high ? 1 : 0});
        page.done();
        ['timerColor', 'chronoColor'].forEach(function(key) {
            var shown = 0;
            page.dom.pickers[key].querySelectorAll('.color-box[data-value]').forEach(function(box) {
                var visible = box.style.visibility !== 'hidden';
                shown += visible ? 1 : 0;
                if (visible !== drawable(box.hex, high)) {
                    fail((high ? 'high' : 'regular') + ' ' + (visible ? 'shows ' : 'hides ') + box.hex);
                }
            });
            if (shown !== 36) { fail((high ? 'high' : 'regular') + ' shows ' + shown + ' in ' + key); }
        });
    });
    if (!failures) { console.log('each mode shows its own 36 colors and hides the rest in place'); }

    // a stored color the mode cannot draw is moved on build, as the watch moves it, and the readout
    // names where it went: every color the pickers place, in both modes
    var checked = 0;
    [false, true].forEach(function(high) {
        SDK_MAP.forEach(function(row) {
            row.forEach(function(cell) {
                var hex = cell[1];
                if (!drawable(hex, false) && !drawable(hex, true)) { return; } // never placed
                var page = buildPage(customFn, {ten: NEVER, min: NEVER, contrast: high ? 1 : 0,
                                                timer: parseInt(hex, 16)});
                page.done();
                var got = ('00000' + page.values.timerColor.toString(16)).slice(-6);
                var want = movedTo(hex, high);
                var readout = page.dom.pickers.timerColor.querySelector('.description');
                if (got !== want) {
                    fail((high ? 'high' : 'regular') + ' kept a stored ' + hex + ' as ' + got +
                         ', the list says ' + want);
                } else if (!readout || readout.textContent.slice(-7) !== '#' + want) {
                    fail('a stored ' + hex + ' moved to ' + want + ' but the readout says "' +
                         (readout && readout.textContent) + '"');
                }
                checked++;
            });
        });
    });
    if (!failures) {
        console.log('a stored color moves into the mode as the watch moves it: ' + checked +
                    ' cases, the readout following');
    }

    // and switching the mode does the same to a pick which no longer fits, both ways round
    var page = buildPage(customFn, {ten: NEVER, min: NEVER, contrast: 0,
                                    timer: 0xffff00, chrono: 0x00aa00});
    var hex = function(key) { return ('00000' + page.values[key].toString(16)).slice(-6); };
    if (hex('chronoColor') !== '00aa00') { fail('regular moved Islamic Green, which it can draw'); }
    page.items.contrast.set(1);
    if (hex('chronoColor') !== '00ff00' || hex('timerColor') !== 'ffff00') {
        fail('switching to high left ' + hex('timerColor') + ' and ' + hex('chronoColor') +
             ', expected ffff00 and 00ff00');
    }
    var shownHigh = page.dom.pickers.timerColor.querySelectorAll('.color-box[data-value]')
        .filter(function(box) { return box.style.visibility !== 'hidden'; })
        .every(function(box) { return drawable(box.hex, true); });
    if (!shownHigh) { fail('after the switch to high, the picker still shows regular\'s colors'); }
    page.items.contrast.set(0);
    if (hex('chronoColor') !== '00ff00') { fail('switching back moved Green, which both can draw'); }
    page.done();
    if (!failures) {
        console.log('switching contrast moves a pick that no longer fits, and leaves one that does');
    }
}

console.log(failures ? failures + ' FAILURES' : 'configuration page holds');
process.exit(failures ? 1 : 0);
