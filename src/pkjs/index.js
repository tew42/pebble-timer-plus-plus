// @file index.js
// @brief Configuration page glue
//
// Builds the Clay page from config.json, keeps the two update thresholds meaningful, and answers
// the watch when it asks what the settings are.
//
// @author Thomas Winkler (tew42)
// @date September 3, 2026
// @bugs No known bugs

var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');

// Answer the watch when it asks for the settings, which it does every time it starts.
//
// Clay sends them once, when the configuration page closes, and that send is dropped if the watch
// app is not running at that moment -- which it usually is not, since the page is opened from the
// phone. The watch would then go on using whatever it had stored until the page next happened to
// be opened while the app was running. The phone is the side which always holds the last save, so
// it is the side that can close the gap.
//
// It reads Clay's own storage key to do it, which looks like reaching inside the library and is
// worth saying why it is not avoidable. Clay exposes no reader for what it has saved:
// getSettings() parses the response the configuration page hands back when it closes, and is no
// use here because no page has been opened. Clay's own generateUrl reads localStorage
// 'clay-settings' directly for exactly the same reason. So this is the same door the library
// uses, not a back one -- but it is an unversioned door, and package.json allows Clay minor
// bumps, so if a future version renames the key this handler goes quiet. That is why the empty
// case logs rather than returning in silence: on the watch side settings.c also gives up without
// a word after three tries, and two silent halves make a feature which disappears with no sign
// anywhere.
//
// The request carries the watch's overrun choice. A page saved before that toggle existed, or never
// saved, has no say in it, and would show the toggle at its default, off: saving the page would
// then quietly take an install from before the toggle off the stopwatch it has always had. So the
// watch's choice is saved as if from the page, through Clay's own setter, and the page shows it.
Pebble.addEventListener('appmessage', function(e) {
    var stored = null;
    try {
        stored = JSON.parse(localStorage.getItem('clay-settings'));
    } catch (err) {
        console.log('Timer++: the saved settings could not be read: ' + err);
    }
    var overrun = (e && e.payload) ? e.payload.overrunChrono : undefined;
    if (overrun !== undefined && !(stored && stored.hasOwnProperty('overrunChrono'))) {
        clay.setSettings('overrunChrono', !!overrun);
        stored = stored || {};
        stored.overrunChrono = !!overrun;
    }
    if (!stored) {
        // nothing has ever been saved, so what the watch already has is the truth -- or the key
        // has moved, which looks identical from here and is why it is worth a line in the log
        console.log('Timer++: nothing saved to answer the watch with');
        return;
    }
    Pebble.sendAppMessage(Clay.prepareSettingsForAppMessage(stored), function() {
        console.log('Timer++: answered the watch with the saved settings');
    }, function(e) {
        console.log('Timer++: could not answer the watch: ' + JSON.stringify(e));
    });
});

var clay = new Clay(clayConfig, function() {
    var clayConfig = this;
    // Clay stringifies this function into the configuration page and calls it there, so nothing
    // else in this file is in scope: anything it needs has to be declared inside it.
    var NEVER = 255;  // must match SETTINGS_NEVER in src/c/settings.h
    var SEC_IN_MIN = 60;

    // Where the two thresholds overlap the watch takes the coarser mode, so a ten second
    // threshold at or beyond the minute one leaves its band empty and does nothing at all. That
    // is well defined, just misleading to look at, so the page switches the ten second setting to
    // Never: the same behavior, said plainly, and the minute threshold just chosen is kept.
    function resolveThresholdOverlap() {
        var tenSecond = clayConfig.getItemByMessageKey("tenSecondUpdatesAbove");
        var minute = clayConfig.getItemByMessageKey("minuteUpdatesAbove");
        if (!tenSecond || !minute) {
            return;
        }
        // parse before comparing: a select's get() hands back the option string, or the number it
        // spells where the item sets serializeValueAs "integer"
        var tenSecondValue = parseInt(tenSecond.get(), 10);
        var minuteValue = parseInt(minute.get(), 10);
        if (tenSecondValue === NEVER || minuteValue === NEVER) {
            return;  // neither mode can crowd the other out
        }
        if (tenSecondValue >= minuteValue * SEC_IN_MIN) {
            tenSecond.set(String(NEVER));
        }
    }

    // The two palettes, and which colors each offers
    // The same rule as settings_accent_rgb in src/c/settings.c, which applies it to whatever the
    // watch has stored, so the page and the watch always agree on the color in force. With either
    // coarse update mode on, the ring gets a band, and only the 24 accents in COARSE_ACCENTS were
    // chosen to stand off it and the black behind the ring; a color outside them moves to the
    // nearest by hue first and lightness second. Otherwise anything goes but black and white,
    // which would vanish into what is behind the ring or the center. Either way black and white
    // become the default green.
    var COARSE_ACCENTS = [
        0x00aaaa, 0x00ff00, 0x5555ff, 0xaa5555, 0x55aa55, 0xaa55aa, 0xaaaa00, 0xff5500,
        0xff00ff, 0xff5555, 0x5555aa, 0x00aaff, 0xaa55ff, 0x00aa00, 0xff00aa, 0x55aaaa,
        0xff55aa, 0xffaa00, 0xaa00aa, 0x55ff00, 0x55aaff, 0xaa00ff, 0xff55ff, 0xffaa55
    ];
    // Which of them each of the 64 colors becomes, a letter for each from a to x, indexed by the
    // color's two-bit channels as rrggbb. Must match COARSE_MOVES in src/c/settings.c; both are
    // printed by tools/hosttest/gen_palette.js, which works them out.
    var COARSE_MOVES = "bkkcnakcnealbbbpdskcekkcneputtbpddsvddfmggputttpjjoihjqwrxjwggrb";
    var DEFAULT_ACCENT = 0x00ff00;  // must match SETTINGS_TIMER_RGB_DEFAULT in src/c/settings.h

    // Channels are Pebble's two-bit levels, taken from the top of each byte as GColorFromHEX
    // takes them
    function colorIndex(value) {
        return ((value >> 18) & 0x30) | ((value >> 12) & 0x0c) | ((value >> 6) & 0x03);
    }

    // The color a pick becomes in a palette: itself if the palette offers it, otherwise its move
    function accentFor(value, coarse) {
        var index = colorIndex(value);
        if (coarse) {
            return COARSE_ACCENTS[COARSE_MOVES.charCodeAt(index) - 97];
        }
        if (index === 0x00 || index === 0x3f) {
            return DEFAULT_ACCENT;
        }
        return [index >> 4, index >> 2, index].reduce(function(sum, c) {
            return sum * 256 + (c & 3) * 0x55;
        }, 0);
    }

    // Whether either coarse update mode is on, which is what decides the palette
    function coarse() {
        return ["tenSecondUpdatesAbove", "minuteUpdatesAbove"].some(function(key) {
            var item = clayConfig.getItemByMessageKey(key);
            return !!item && parseInt(item.get(), 10) !== NEVER;
        });
    }

    // Draw the swatches as hexagons, edged in black, and mark the one picked
    // The edge is a black ring laid over the inside of each hexagon, between its outline and the
    // same outline shrunk by a twelfth about the middle, so two neighbors meet across a line of
    // twice that. The hexagons clip away the shadow Clay marks the pick with, so it gets a dot in
    // the middle instead, white in a dark ring to show on any color. Clay paints each swatch with
    // an inline background shorthand, which would clear the dot without the !important.
    var HEX_CLASS = "hex-map";
    function addHexStyle() {
        if (document.getElementById(HEX_CLASS)) {
            return;
        }
        var hexagon = "polygon(50% 0, 100% 25%, 100% 75%, 50% 100%, 0 75%, 0 25%)";
        var ring = "polygon(evenodd, 50% 0, 100% 25%, 100% 75%, 50% 100%, 0 75%, 0 25%, 50% 0, " +
                   "50% 4%, 4% 27%, 4% 73%, 50% 96%, 96% 73%, 96% 27%, 50% 4%)";
        var style = document.createElement("style");
        style.id = HEX_CLASS;
        style.textContent =
            "." + HEX_CLASS + " .color-box[data-value]{position:relative;-webkit-clip-path:" + hexagon +
            ";clip-path:" + hexagon + "}" +
            "." + HEX_CLASS + " .color-box[data-value]::after{content:'';position:absolute;top:0;right:0;" +
            "bottom:0;left:0;background:#000;-webkit-clip-path:" + ring + ";clip-path:" + ring +
            "}" +
            "." + HEX_CLASS + " .color-box.selected{box-shadow:none;background-image:" +
            "radial-gradient(circle,#fff 0 14%,#000 14% 22%,transparent 22%) !important}";
        document.head.appendChild(style);
    }

    // Lay a picker out like the SDK's color map, as interlocking hexagons
    // config.json holds the map from the SDK's color picker page on a grid of half hexagons: each
    // color sits in one cell with an empty one after it, and alternate rows start half a hexagon
    // in. Clay draws every cell the same width, so each swatch is widened here over the gap after
    // it, and made the height of a pointed hexagon that wide. A negative margin of a quarter of
    // that height lets Clay's floats stack the rows three quarters of a hexagon apart, so each
    // row's points sit in the notches of the next, and the picker grows to hold the rows at that
    // pitch plus the last row's points. The empty cells are set to the pitch as well: left at
    // Clay's height they stand taller than a row, and the floats after them snag on them.
    function shapePicker(picker) {
        var cells = picker.querySelectorAll(".color-box");
        if (!cells.length) {
            return;
        }
        var width = 2 * parseFloat(cells[0].style.width);  // a hexagon, in % of the picker's width
        var columns = Math.round(200 / width);
        var rows = Math.round(cells.length / columns);
        var hexHeight = width * 2 / Math.sqrt(3);  // also in % of the width
        var pickerHeight = rows * hexHeight * 3 / 4 + hexHeight / 4;
        picker.classList.add(HEX_CLASS);
        for (var ii = 0; ii < cells.length; ii++) {
            var box = cells[ii];
            // a height in % is of the picker's height, a margin in % of its width
            if (!box.getAttribute("data-value")) {
                box.style.height = (hexHeight * 3 / 4 / pickerHeight * 100) + "%";
                continue;
            }
            var gap = box.nextElementSibling;
            if (gap && !gap.getAttribute("data-value")) {
                gap.style.display = "none";
            }
            box.style.width = width + "%";
            box.style.height = (hexHeight / pickerHeight * 100) + "%";
            box.style.marginBottom = (-hexHeight / 4) + "%";
        }
        var wrap = picker.querySelector(".color-box-wrap");
        if (wrap) {
            wrap.style.paddingBottom = pickerHeight + "%";
        }
    }

    // Offer each picker only the colors the palette in force offers, and move a pick it does not
    // The swatches keep their places in the map and are only hidden, so switching palettes does not
    // shuffle what is left.
    function applyPalette(pickers) {
        var inForce = coarse();
        pickers.forEach(function(entry) {
            var boxes = entry.picker.querySelectorAll(".color-box[data-value]");
            for (var ii = 0; ii < boxes.length; ii++) {
                var value = parseInt(boxes[ii].getAttribute("data-value"), 10);
                boxes[ii].style.visibility = (accentFor(value, inForce) === value) ? "" : "hidden";
            }
            var current = parseInt(entry.item.get(), 10);
            var moved = accentFor(current, inForce);
            if (moved !== current) {
                entry.item.set(moved);
            }
        });
    }

    // Both color pickers, with the elements they are drawn in
    function colorPickers() {
        var pickers = [];
        ["timerColor", "chronoColor"].forEach(function(key) {
            var item = clayConfig.getItemByMessageKey(key);
            if (item && item.$element && item.$element[0]) {
                pickers.push({item: item, picker: item.$element[0]});
            }
        });
        return pickers;
    }

    clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
        var pickers = colorPickers();
        addHexStyle();
        pickers.forEach(function(entry) {
            shapePicker(entry.picker);
        });
        // the overlap first, since it can switch a threshold the palette then follows. It also
        // normalizes a redundant pair which was stored before this ran.
        function followThresholds() {
            resolveThresholdOverlap();
            applyPalette(pickers);
        }
        followThresholds();
        ["tenSecondUpdatesAbove", "minuteUpdatesAbove"].forEach(function(key) {
            var item = clayConfig.getItemByMessageKey(key);
            if (item) {
                item.on("change", followThresholds);
            }
        });
    });
});
