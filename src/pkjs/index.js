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
Pebble.addEventListener('appmessage', function() {
    var stored = null;
    try {
        stored = JSON.parse(localStorage.getItem('clay-settings'));
    } catch (e) {
        console.log('Timer++: the saved settings could not be read: ' + e);
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
    // Never: the same behaviour, said plainly, and the minute threshold just chosen is kept.
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

    // Clay draws the colour picker as bare swatches and nothing else. That is enough on a desktop
    // and not enough on a phone: a good many of the accents on offer differ by one two-bit channel,
    // and on an OLED panel those are close enough together to be a guess. So each swatch carries
    // its own hex code, and whichever one is in force is named underneath the picker.
    //
    // All sixty-four of Pebble's colours are named here rather than just the thirty this page
    // offers, so the labels survive the layout being trimmed and still read on the black and white
    // layouts Clay substitutes when the watch has no colour screen.
    var COLOR_NAMES = {
        "000000": "Black", "000055": "Oxford Blue", "0000aa": "Duke Blue", "0000ff": "Blue",
        "005500": "Dark Green", "005555": "Midnight Green", "0055aa": "Cobalt Blue",
        "0055ff": "Blue Moon", "00aa00": "Islamic Green", "00aa55": "Jaeger Green",
        "00aaaa": "Tiffany Blue", "00aaff": "Vivid Cerulean", "00ff00": "Green",
        "00ff55": "Malachite", "00ffaa": "Medium Spring Green", "00ffff": "Cyan",
        "550000": "Bulgarian Rose", "550055": "Imperial Purple", "5500aa": "Indigo",
        "5500ff": "Electric Ultramarine", "555500": "Army Green", "555555": "Dark Gray",
        "5555aa": "Liberty", "5555ff": "Very Light Blue", "55aa00": "Kelly Green",
        "55aa55": "May Green", "55aaaa": "Cadet Blue", "55aaff": "Picton Blue",
        "55ff00": "Bright Green", "55ff55": "Screamin Green", "55ffaa": "Medium Aquamarine",
        "55ffff": "Electric Blue", "aa0000": "Dark Candy Apple Red", "aa0055": "Jazzberry Jam",
        "aa00aa": "Purple", "aa00ff": "Vivid Violet", "aa5500": "Windsor Tan",
        "aa5555": "Rose Vale", "aa55aa": "Purpureus", "aa55ff": "Lavender Indigo",
        "aaaa00": "Limerick", "aaaa55": "Brass", "aaaaaa": "Light Gray",
        "aaaaff": "Baby Blue Eyes", "aaff00": "Spring Bud", "aaff55": "Inchworm",
        "aaffaa": "Mint Green", "aaffff": "Celeste", "ff0000": "Red", "ff0055": "Folly",
        "ff00aa": "Fashion Magenta", "ff00ff": "Magenta", "ff5500": "Orange",
        "ff5555": "Sunset Orange", "ff55aa": "Brilliant Rose", "ff55ff": "Shocking Pink",
        "ffaa00": "Chrome Yellow", "ffaa55": "Rajah", "ffaaaa": "Melon",
        "ffaaff": "Rich Brilliant Lavender", "ffff00": "Yellow", "ffff55": "Icterine",
        "ffffaa": "Pastel Yellow", "ffffff": "White"
    };

    // The hex a swatch stands for, back out of the decimal Clay writes on it
    function swatchHex(box) {
        var hex = parseInt(box.getAttribute("data-value"), 10).toString(16);
        while (hex.length < 6) {
            hex = "0" + hex;
        }
        return hex;
    }

    // Black or white lettering, whichever the swatch can carry
    // Measured off the rendered element rather than worked out from the hex, because Clay paints
    // the swatches through its sunlight map unless the item opts out of it, and the label has to
    // stay legible on whichever of the two the page happens to be showing.
    function swatchInk(box) {
        var channels = window.getComputedStyle(box).backgroundColor.match(/\d+/g);
        if (!channels || channels.length < 3) {
            return "#000";
        }
        function linear(text) {
            var value = parseInt(text, 10) / 255;
            return (value <= 0.04045) ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4);
        }
        var luminance = 0.2126 * linear(channels[0]) + 0.7152 * linear(channels[1]) +
                        0.0722 * linear(channels[2]);
        return (luminance > 0.3) ? "#000" : "#fff";
    }

    // Label one picker's swatches, and give it a readout naming the current choice
    function describePicker(picker) {
        // Clay's own class, so the readout is spaced and coloured the way a description would be.
        // It goes ahead of the picker rather than after it because the picker is position fixed
        // and takes up no room in the flow, so appending would leave the name floating in a gap.
        var readout = document.createElement("div");
        readout.className = "description";
        picker.insertBefore(readout, picker.querySelector(".picker-wrap"));
        function announce(box) {
            var hex = swatchHex(box);
            readout.textContent = (COLOR_NAMES[hex] || "Custom") + "  #" + hex;
        }
        var boxes = picker.querySelectorAll(".color-box[data-value]");
        for (var ii = 0; ii < boxes.length; ii++) {
            var box = boxes[ii];
            var hex = swatchHex(box);
            box.textContent = hex;
            box.title = COLOR_NAMES[hex] || hex;
            // appended, so Clay's own width, height and background survive. The font shorthand
            // also clears the italics the swatch inherits from being an <i>.
            box.style.cssText += ";display:flex;align-items:center;justify-content:center" +
                ";font:normal 0.6rem/1 monospace;letter-spacing:0;color:" + swatchInk(box);
            box.addEventListener("click", function(event) {
                announce(event.currentTarget);
            });
        }
        // Clay sets each item's value, and so the selected class, while it builds, so there is
        // already a choice to name by the time this runs
        var selected = picker.querySelector(".color-box.selected");
        if (selected) {
            announce(selected);
        }
    }

    // Name every colour picker on the page
    function describeColorPickers() {
        var pickers = document.querySelectorAll(".component-color");
        for (var ii = 0; ii < pickers.length; ii++) {
            describePicker(pickers[ii]);
        }
    }

    clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
        // also normalises a redundant pair which was stored before this ran
        resolveThresholdOverlap();
        describeColorPickers();
        var tenSecond = clayConfig.getItemByMessageKey("tenSecondUpdatesAbove");
        var minute = clayConfig.getItemByMessageKey("minuteUpdatesAbove");
        if (tenSecond) {
            tenSecond.on("change", resolveThresholdOverlap);
        }
        if (minute) {
            minute.on("change", resolveThresholdOverlap);
        }
    });
});
