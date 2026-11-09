// Work out where coarse mode moves each of Pebble's 64 colors, and write it to palettes.json
// Coarse mode offers only the 24 accents in PAIRS, each drawn with its own band. A color outside
// them moves to the one it is nearest to, measured in OKLCH with the hue difference weighted up,
// so a move keeps the hue first and the lightness second. Black and white have neither, and take
// the default green. The watch and the configuration page carry the result as a table rather than
// doing this float math themselves; jstest.js runs this again to hold palettes.json to it.
//
// node gen_palette.js            rewrite palettes.json and print the tables to paste
// require('./gen_palette.js')    the rule, for the tests
var fs = require('fs');
var path = require('path');

// The coarse accents and their bands, in the order chosen, which is also the order a tie goes by
var PAIRS = [
    ['00aaaa', '005555'], ['00ff00', '005500'], ['5555ff', '0000ff'], ['aa5555', 'aa0000'],
    ['55aa55', '005500'], ['aa55aa', '550055'], ['aaaa00', '555500'], ['ff5500', 'aa0000'],
    ['ff00ff', '550055'], ['ff5555', 'ff0000'], ['5555aa', '0000aa'], ['00aaff', '005555'],
    ['aa55ff', '5500aa'], ['00aa00', '005500'], ['ff00aa', '550055'], ['55aaaa', '005555'],
    ['ff55aa', 'aa0055'], ['ffaa00', '555500'], ['aa00aa', '550055'], ['55ff00', '005500'],
    ['55aaff', '0055aa'], ['aa00ff', '5500aa'], ['ff55ff', '550055'], ['ffaa55', 'aa5500']
];
var DEFAULT = '00ff00';
var HUE_WEIGHT = 4;

var LEVELS = ['00', '55', 'aa', 'ff'];
var ALL = [];
LEVELS.forEach(function(r) {
    LEVELS.forEach(function(g) {
        LEVELS.forEach(function(b) { ALL.push(r + g + b); });
    });
});

// Lightness, chroma and hue angle, by way of OKLab
function oklch(hex) {
    var lin = [0, 2, 4].map(function(at) {
        var c = parseInt(hex.substr(at, 2), 16) / 255;
        return (c <= 0.04045) ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
    });
    var l = Math.cbrt(0.4122214708 * lin[0] + 0.5363325363 * lin[1] + 0.0514459929 * lin[2]);
    var m = Math.cbrt(0.2119034982 * lin[0] + 0.6806995451 * lin[1] + 0.1073969566 * lin[2]);
    var s = Math.cbrt(0.0883024619 * lin[0] + 0.2817188376 * lin[1] + 0.6299787005 * lin[2]);
    var a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    var b = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    return {L: 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            C: Math.hypot(a, b), h: Math.atan2(b, a)};
}

// The squared distance between two colors, the hue term counting HUE_WEIGHT times over
// The hue difference is taken as an arc at the two colors' chroma, so it fades to nothing for a
// gray, which then moves by lightness and chroma alone.
function distance(x, y) {
    var p = oklch(x);
    var q = oklch(y);
    var dH = 2 * Math.sqrt(p.C * q.C) * Math.sin((q.h - p.h) / 2);
    return Math.pow(q.L - p.L, 2) + Math.pow(q.C - p.C, 2) + HUE_WEIGHT * dH * dH;
}

// The coarse accent a color becomes
function coarseAccent(hex) {
    if (hex === '000000' || hex === 'ffffff') {
        return DEFAULT;
    }
    var best = null;
    var bestDistance = Infinity;
    PAIRS.forEach(function(pair) {
        var d = distance(hex, pair[0]);
        if (d < bestDistance) {  // strictly, so a tie keeps the earlier pair
            best = pair[0];
            bestDistance = d;
        }
    });
    return best;
}

function moves() {
    var out = {};
    ALL.forEach(function(hex) { out[hex] = coarseAccent(hex); });
    return out;
}

module.exports = {PAIRS: PAIRS, DEFAULT: DEFAULT, ALL: ALL, moves: moves};

if (require.main === module) {
    var table = moves();
    var fixture = {
        _note: [
            'Coarse mode: its 24 accents with their bands, and the accent each of the 64 colors',
            'becomes. Written by gen_palette.js; read by jstest.js and, through gen_configopts.js,',
            'by settingstest.c and shadetest.c, so the page and the watch are held to one table.'
        ],
        pairs: PAIRS,
        coarse: table
    };
    fs.writeFileSync(path.join(__dirname, 'palettes.json'),
                     JSON.stringify(fixture, null, 2) + '\n');
    var index = ALL.map(function(hex) {
        return PAIRS.findIndex(function(pair) { return pair[0] === table[hex]; });
    });
    var rows = [];
    for (var r = 0; r < 4; r++) {
        rows.push('    ' + index.slice(r * 16, r * 16 + 16).join(', ') + ',  // red ' + LEVELS[r]);
    }
    process.stdout.write('settings.c COARSE_MOVES:\n' + rows.join('\n') + '\n\n' +
                         'index.js COARSE_MOVES: "' +
                         index.map(function(ii) { return String.fromCharCode(97 + ii); }).join('') +
                         '"\n');
}
