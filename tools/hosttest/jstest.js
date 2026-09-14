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
                instantStart: 10006};
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
                 timerColor: 0x00ff00, chronoColor: 0xff0000, instantStart: 10};
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
            console.log('  ok: the four saved settings go back under their own message keys');
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

// The colour pickers offer only accents with room to be shaded: full brightness (one channel at
// ff) and at least two thirds saturation (the lowest channel no higher than 55). Anything else
// shades to a band which lands on the ring or on the colour behind it. drawing.c has a fallback
// for whatever an old install sends, but the palette itself must never need it.
['timerColor', 'chronoColor'].forEach(function(key) {
    var item = itemFor(key);
    if (!item.layout) {
        fail(key + ' has no restricted layout, so the full palette is offered');
        return;
    }
    var swatches = [];
    item.layout.forEach(function(row) {
        row.forEach(function(c) { if (c) { swatches.push(c); } });
    });
    var seen = {};
    swatches.forEach(function(hex) {
        if (!/^[0-9a-f]{6}$/.test(hex)) {
            fail(key + ' swatch "' + hex + '" is not a lowercase 6 digit hex');
            return;
        }
        if (seen[hex]) { fail(key + ' repeats ' + hex); }
        seen[hex] = true;
        var ch = [0, 2, 4].map(function(i) {
            return Math.floor(parseInt(hex.substr(i, 2), 16) * 3 / 255);
        });
        var max = Math.max.apply(Math, ch), min = Math.min.apply(Math, ch);
        if (max !== 3 || min > 1) {
            fail(key + ' offers ' + hex + ', which has no room to be shaded');
        }
    });
    if (swatches.length !== 30) {
        fail(key + ' offers ' + swatches.length + ' colours, expected 30');
    }
    if (!seen[item.defaultValue]) {
        fail(key + ' defaults to ' + item.defaultValue + ', not in its palette');
    }
});
if (!failures) { console.log('both colour pickers offer 30 shadeable colours, defaults included'); }

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

// A stand-in for the built page. Clay's `val` manipulator returns a number when the item sets
// serializeValueAs "integer" and the option string otherwise, and its set() only fires "change"
// when the value really changes -- both worth reproducing, the second because it is what stops
// a correcting handler from recursing forever.
function buildPage(customFn, ten, min, asNumber) {
    var values = {tenSecondUpdatesAbove: ten, minuteUpdatesAbove: min};
    var changed = {tenSecondUpdatesAbove: [], minuteUpdatesAbove: []};
    var afterBuild = [];
    var sets = 0;
    var items = {};
    Object.keys(values).forEach(function(key) {
        items[key] = {
            get: function() { return asNumber ? values[key] : String(values[key]); },
            set: function(value) {
                if (String(value) === String(values[key])) { return; }
                values[key] = parseInt(value, 10);
                if (++sets > 20) { throw new Error('set() looped'); }
                changed[key].forEach(function(handler) { handler(); });
            },
            on: function(event, handler) {
                if (event === 'change') { changed[key].push(handler); }
            }
        };
    });
    customFn.call({
        EVENTS: { AFTER_BUILD: 'AFTER_BUILD' },
        getItemByMessageKey: function(key) { return items[key]; },
        on: function(event, handler) { if (event === 'AFTER_BUILD') { afterBuild.push(handler); } }
    });
    afterBuild.forEach(function(handler) { handler(); });
    return { values: values, sets: sets };
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
                    page = buildPage(customFn, ten, min, asNumber);
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

console.log(failures ? failures + ' FAILURES' : 'configuration page holds');
process.exit(failures ? 1 : 0);
