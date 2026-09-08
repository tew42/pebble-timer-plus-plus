// @file index.js
// @brief Configuration page glue
//
// Builds the Clay page from config.json and keeps the two update thresholds meaningful.
//
// @author Thomas Winkler (tew42)
// @date September 3, 2026
// @bugs No known bugs

var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');

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

    clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
        // also normalises a redundant pair which was stored before this ran
        resolveThresholdOverlap();
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
