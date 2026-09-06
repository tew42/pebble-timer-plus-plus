var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');

// Must match SETTINGS_NEVER in src/c/settings.h
var NEVER = 255;

var clay = new Clay(clayConfig, function() {
    var clayConfig = this;

    // Minute updates must begin strictly above where ten second updates do, or the band between
    // them is empty and the ten second setting does nothing: the watch would go straight from
    // per-second to per-minute. So a ten second threshold rules out the minute options at or
    // below it. Clay can only disable a whole item rather than individual options, so raise the
    // value instead.
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
        var minimumMinutes = Math.floor(tenSecondValue / 60) + 1;
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
