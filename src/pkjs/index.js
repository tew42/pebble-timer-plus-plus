var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');

// Must match SETTINGS_NEVER in src/c/settings.h
var NEVER = 255;

var clay = new Clay(clayConfig, function() {
    var clayConfig = this;

    // Minute updates should only begin at or above where ten second updates already have, so a
    // ten second threshold over a minute rules out the shorter minute options. Clay can only
    // disable a whole item rather than individual options, so raise the value instead.
    function enforceThresholdOrder() {
        var tenSecond = clayConfig.getItemByMessageKey("tenSecondUpdatesAbove");
        var minute = clayConfig.getItemByMessageKey("minuteUpdatesAbove");
        if (!tenSecond || !minute) {
            return;
        }
        var tenSecondValue = tenSecond.get();
        if (tenSecondValue === NEVER) {
            return;  // there is no coarse threshold to stay above
        }
        var minimumMinutes = Math.ceil(tenSecondValue / 60);
        if (minute.get() < minimumMinutes) {
            minute.set(minimumMinutes);
        }
    }

    clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
        enforceThresholdOrder();
        var tenSecond = clayConfig.getItemByMessageKey("tenSecondUpdatesAbove");
        var minute = clayConfig.getItemByMessageKey("minuteUpdatesAbove");
        if (tenSecond) {
            tenSecond.on("change", enforceThresholdOrder);
        }
        if (minute) {
            minute.on("change", enforceThresholdOrder);
        }
    });
});
