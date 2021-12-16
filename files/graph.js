/* A simple in-browser app to browse counters out of istatd. */
/* Copyright 2011 IMVU, Inc. Author: jwatte@imvu.com */
/*

todo:
- arrange colors and counter ordering explicitly
- render modes: area, stacked, line, min/max (in addition to "bands")
*/

/*  HEY Let's highjack DYGRAPH plugins! */
Dygraph.PLUGINS = Dygraph.PLUGINS.slice(1);
Dygraph.PLUGINS.unshift(ISTATD_Legend);

var pageIsHidden;
var visibilityChange;
var hidden;
var visibilitySupported = true;
if (typeof document.hidden !== "undefined") {
    pageIsHidden = function() {
        return document.hidden;
    }
    visibilityChange = "visibilitychange";
    hidden = "hidden";
} else if (typeof document.mozHidden !== "undefined") {
    pageIsHidden = function() {
        return document.mozHidden;
    }
    visibilityChange = "mozvisibilitychange";
    hidden = "mozHidden";
} else if (typeof document.msHidden !== "undefined") {
    pageIsHidden = function() {
        return document.msHidden;
    }
    visibilityChange = "msvisibilitychange";
    hidden = "msHidden";
} else if (typeof document.webkitHidden !== "undefined") {
    pageIsHidden = function() {
        return document.webkitHidden;
    }
    visibilityChange = "webkitvisibilitychange";
    hidden = "webkitHidden";
} else {
    // Assume the browser does not support page visibility
    pageIsHidden = function() {
        return false;
    }
    visibilitySupported = false;
}

var document_hidden = visibilitySupported ? document[hidden] : false;

CONSOLE_LOG_ALERT = false;
if(typeof(console) === 'undefined') {
    console = {};
}

if(!console.log) {
    console.log = function() {
        if(CONSOLE_LOG_ALERT) {
            alert(Array.prototype.join.call(arguments, ' '));
        }
    }
}

/* Event handlers and other calls into this application
   from the surrounding DOM should be wrapped in guard().
   This lets us display error messages and track exceptions 
   in an explicit way that improves user experience and 
   makes the app easier to debug.

   Guard takes a function, and returns a function that does 
   whatever the original function did, with appropriate 
   wrapping for error/exception handling.
 */
function guard(f) {
    return function() {
        try {
            return f.apply(this, arguments);
        }
        catch (e) {
            if (typeof(console) !== 'undefined') {
                console.log(e.message);
            }

            var stack = e.stack;
            if(typeof(stack) === 'undefined') {
                var callee = arguments.callee;
                stack = [];
                while(callee) {
                    var name = callee.toString().match(/function\s*([^(])*/)[1];
                    name = name || '<anonymous function>';
                    name += '(' + Array.prototype.join.call(callee.arguments, ', ') + ')';
                    stack.push(name);
                    callee = callee.caller;
                }
                stack = stack.join('\n');
            }

            var str = 'Error in ' + f.name + ':\n' + e.message + '\n\n' + stack;
            errorDialog(str);
        }
    }
}

function htmlescape(str) {
    str = str.replace(/\&/g, '&amp;').replace(/</g, '&lt;').replace(/\n/g, '<br>\n');
    return str;
}

function arrayKeys(a) {
   var ret = new Array();
   for (k in a) {
      ret.push(k);
   }
   ret.sort();
   return ret;
}

/* Actually show data from a little while ago, to avoid showing 
   current, possibly not-yet-full, buckets. */
function getAdjustedNow() {
    return new Date((new Date().getTime())-10*1000);
}

function errorDialog(str, container) {
    console.log('errorDialog ' + str);
    if (!container) {
        container = $('body');
    }
    else {
        container = $(container);
    }
    var $div = $('.dialog.error');
    var append = false;
    if (!$div || $div.length == 0) {
        $div = $("<div class='dialog error'><span class='closebox buttonbox'/><div class='scroll'><span class='text'/></div></div>");
    }
    else {
        append = true;
        $div.detach();
    }
    str = htmlescape(str);
    var $span = $('span.text', $div);
    if (append) {
        str = $span.html() + "<br/><br/>" + str;
    }
    $span.html(str);
    $div.prependTo(container);
    //  can't use guard() in error because of recursion
    $('span.closebox', $div).click(function() {
        $div.remove();
    });
    nesting = 0;
    $container = $('div#progressbar').show();
    $progress = $('.progressfg', $container);
    $progress.animate({width: 0}, 500.0, function() {
        $container.css('visibility', 'hidden');
    });
}

function promptDialog(prmp, cb) {
    console.log('prompt: ' + prmp);

    $('.dialog.prompt').detach();
    var $div = $("<div class='dialog prompt'><span class='closebox buttonbox'></span><div class='text'></div>" +
        "<input type='text' class='answer'/><div class='tbutton' name='prompt_done'>OK</div></div>");
    str = htmlescape(prmp);
    $('div.text', $div).html(str);
    $div.prependTo($('body'));
    $('span.closebox', $div).click(function() {
        $div.remove();
    });
    $('.answer', $div).val('');
    var eHandler = guard(function(ev) {
        ev.stopPropagation();
        if(cb($('.answer', $div).val()) !== false) {
            $div.remove();
        }
    });
    $('.tbutton', $div).click(eHandler);
    $('.answer', $div).keydown(function(ev) {
        if (ev.which == 13) {
            $('#counter_jstree').jstree("deselect_all");
            eHandler(ev);
        }
    });
    return $div;
}

function choiceDialog(text, options, cb) {
    console.log('options: ' + text);
    $('.dialog.prompt').detach();
    var buttons = [];
    for (var k in options) {
        buttons.push("<div class='tbutton' name='" + k + "'>" + htmlescape(options[k]) + "</div>");
    }
    buttons = buttons.join("");
    var $div = $("<div class='dialog prompt'><span class='closebox buttonbox'></span><div class='text'></div>" +
        "<div class='buttonrow choices'>" + buttons + "</div></div>");
    str = htmlescape(text);
    $('div.text', $div).html(str);
    $div.prependTo($('body'));
    $('span.closebox', $div).click(function() {
        $div.remove();
    });
    var eHandler = guard(function(ev) {
        ev.stopPropagation();
        var choice = $(this).attr('name');
        console.log('choice: ' + choice);
        cb(choice);
        $div.remove();
    });
    $('.tbutton', $div).click(eHandler);
    return $div;
}


//////////////////////////////
// begin date hack
function promptDialogN(prmp, fields, submitCallback) {
    console.log('prompt: ' + prmp);

    $('.dialog.prompt').detach();
    var $div = $("<div class='dialog prompt'><span class='closebox buttonbox'></span><div class='text'></div>" +
        "<div class='inputs'></div><div class='tbutton' name='prompt_done'>OK</div></div>");
    str = htmlescape(prmp);
    $('div.text', $div).html(str);
    $inputs = $('div.inputs', $div);
    var fldNames = [];
    for (var k in fields) {
        fldNames.push(k);
        $input = $("<div class='input " + k + "'><label for='" + k + "'>" + fields[k] +
            "</label> <input type='text' class='answer " + k + "' name='" +
            k + "'/></div> ");
        $input.appendTo($inputs);
    }
    $div.prependTo($('body'));
    $('span.closebox', $div).click(function() {
        $div.remove();
    });
    $('.answer', $div).val('');
    var eHandler = guard(function(ev) {
        ev.stopPropagation();
        var kv = {};
        console.log('fldNames: ' + JSON.stringify(fldNames));
        for (var k in fldNames) {
            var fn = fldNames[k];
            kv[fn] = $('.answer.' + fn, $inputs).val();
        }
        if(submitCallback(kv) !== false) {
            $div.remove();
        }
    });
    $('.tbutton', $div).click(eHandler);
    $('.answer', $div).keydown(function(ev) {
        if (ev.which == 13) {
            $('#counter_jstree').jstree("deselect_all");
            eHandler(ev);
        }
    });
    return $div;
}

function parseDate(x) {
    if (!x || typeof(x) != 'string') {
        return false;
    }
    var ret = new Date();
    var captures = /^ *(20[0-9][0-9])-0?(1?[0-9])-0?([123]?[0-9]) +([012][0-9]):([0-5][0-9]) *$/.exec(x);
    if (captures) {
        ret.setFullYear(parseInt(captures[1], 10));
        ret.setMonth(parseInt(captures[2], 10)-1);
        ret.setDate(parseInt(captures[3], 10));
        ret.setHours(parseInt(captures[4], 10), parseInt(captures[5], 10));
        return ret;
    }
    captures = /^ *(20[0-9][0-9])-0?(1?[0-9])-0?([123]?[0-9]) *$/.exec(x);
    if (captures) {
        ret.setFullYear(parseInt(captures[1], 10));
        ret.setMonth(parseInt(captures[2], 10)-1);
        ret.setDate(parseInt(captures[3], 10));
        return ret;
    }
    return false;
}

function resetDateRange(dFrom, dTo) {
    var ft = dFrom.getTime();
    var tt = dTo.getTime();
    if (tt < 1000000000) {
        tt = getAdjustedNow().getTime();
    }
    if (ft >= tt || ft < tt - 5 * 366 * 25 * 60 * 60 * 1000) {
        ft = tt - 15 * 60 * 1000;
    }
    theTimeSlider.setManual(true);
    theOriginalDates.start = dFrom;
    theOriginalDates.stop = dTo;
    restoreOriginalInterval();
    setAutoRefresh(false);
    calcReloadInterval();
    theGrid.reloadAll();
}

function select_daterange() {
    promptDialogN('Please enter a date range (YYYY-MM-DD [HH:MM]):',
        {'from': 'From:',
        'to': 'To:',
        'maxSamples': 'Max Samples:<br/>(Optional. If not given, defaults to graph width.)'},
        guard(function(range) {
            console.log('select_daterange(' + JSON.stringify(range) + ')');
            var dFrom = parseDate(range.from);
            var dTo = parseDate(range.to);
            if (!dFrom) {
                errorDialog('The start date is not valid.');
                return false;
            }
            else if (!dTo) {
                errorDialog('The end date is not valid.');
                return false;
            }
            else if (dFrom.getTime() >= dTo.getTime()) {
                errorDialog('The end date must be after the start date.');
                return false;
            }
            else {
                if(!range.maxSamples.match(/^[0-9]*$/)) {
                    errorDialog('Max sample count must be a positive integer, or left blank.');
                    return false;
                }
                theGraphMaxSamples = parseInt(range.maxSamples);
                if(isNaN(theGraphMaxSamples)) {
                    theGraphMaxSamples = undefined;
                }

                resetDateRange(dFrom, dTo);
                return true;
            }
        })
    ).addClass('select_daterange');

}
// end date hack
//////////////////////////////

//////////////////////////////
// xref hack

function xref_change()
{
    xrefSelectDialog('Alter graph to use cross-references instead. See source for more details.', change_counters);
}


function xrefSelectDialog(prmp, cb) {
    console.log('xrefSelect: ' + prmp);

    $('.dialog.prompt').detach();
    var $div = $("<div class='dialog prompt'><span class='closebox buttonbox'></span><div class='text'></div>" +
        "<div class='inputs'></div><div class='tbutton' id='prompt_change' name='prompt_change'>Change</div><div class='tbutton' id='prompt_add' name='prompt_add'>Add</div></div>");
    str = htmlescape(prmp);
    $('div.text', $div).html(str);
    $inputs = $('div.inputs', $div);
    $inputs.append('From ');
    $input = $("<select id=\"xrefFrom\"><option selected=\"selected\">Xref From</option></select>");
    $input.appendTo($inputs);
    $inputs.append(' to ');
    $input = $("<select multiple size=10 id=\"xrefTo\"><option selected=\"selected\">Xref To</option></select>");
    $input.appendTo($inputs);

    $div.prependTo($('body'));
    $('span.closebox', $div).click(function() {
        $div.remove();
    });
    $('.answer', $div).val('');
    var eHandler = guard(function(ev) {
        ev.stopPropagation();
        if (ev.target) {
            targ = ev.target;
        } else if (ev.srcElement) {
            targ = ev.srcElement;
        }
        if (targ.nodeType == 3) { // safari bug
            targ = targ.parentNode;
        }

        var xref_from = document.getElementById('xrefFrom');
        var xref_from_selected = xref_from.options[xref_from.selectedIndex].text;
        var xref_to = document.getElementById('xrefTo');

        if (targ.id == "prompt_change") {
            var xref_to_selected = xref_to.options[xref_to.selectedIndex].text;
            cb(xref_from_selected + ":" + xref_to_selected, 0);
        } else {
            // iterate over all selections when adding and add them individually
            for (x=0 ; x < xref_to.options.length ; x++)
            {
                if (xref_to.options[x].selected)
                {
                    cb(xref_from_selected + ":" + xref_to.options[x].text, 1);
                }
            }
        }

        $div.remove();
    });
    $('.tbutton', $div).click(eHandler);
    $('.answer', $div).keydown(function(ev) {
        if (ev.which == 13) {
            $('#counter_jstree').jstree("deselect_all");
            eHandler(ev);
        }
    });

    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState == 4) {
            var data = JSON.parse(xhr.responseText);
            var x = computeXref(data.agents);
            populateXrefFrom(x, "");
            document.getElementById('xrefFrom').onchange = function() { populateXrefTo(x); };
        }
    }
    xhr.open('GET', '/?a=*', true);
    xhr.send();
}


function populateXrefFrom(xrefs, filter) {
    var xref_from = document.getElementById('xrefFrom');
    var graphed_counters = getGraphedCounters();
    var option_categories = new Array();
    for (var cat in xrefs) {
        for (var ctr in graphed_counters) {
            var counter = graphed_counters[ctr];
            var category = String(cat);
            if (counter.substr(-1 * category.length) == category) {
                option_categories[String(cat)] = 1;
            }
        }
    }
    for (var catStr in option_categories)  {
       xref_from.options[xref_from.options.length] = new Option(catStr, catStr);
    }
}

function populateXrefTo(xrefs) {
    var xref_from = document.getElementById('xrefFrom');
    var xref_from_selected = xref_from.options[xref_from.selectedIndex].text;
    var xref_to = document.getElementById('xrefTo');
    xref_to.options.length = 1;
    var xref_array = new Array();
    for (var cat in xrefs[xref_from_selected]) {
        xref_array[xref_array.length] = String(cat);
    }

    xref_array.sort();
    for (var k in xref_array) {
        xref_to.options[xref_to.options.length] = new Option(xref_array[k], xref_array[k]);
    }
}

function computeXref(x) {
    var ret = new Array();
    var empty = [];
    for (var ix in x) {
        var xx = x[ix];
        if ("istatd_categories" in xx) {
           var categories = xx["istatd_categories"].split(",");
           for(var cat in categories) {
               if (!(categories[cat] in ret)) {
                   ret[categories[cat]] = new Array();
               }
               for (var cat2 in categories) {
                   if (!(categories[cat2] in ret[categories[cat]])) {
                       ret[categories[cat]][categories[cat2]] = 1;
                   }
               }
           }
        }
    }
    for (var k in ret) {
       delete ret[k][k];
       ret[k].sort();
    }
    return ret;
}

function getGraphedCounters()
{
    var all_counters = new Array();
    for (var k in theGrid._allGraphs) {
        graph = theGrid._allGraphs[k]
        for (var path in graph._series) {
            all_counters[path] = 1;
        }
    }
    return(arrayKeys(all_counters));
}


//
//////////////////////////////

function done() {
    console.log('done');
}

var nesting = 0;

function begin(label) {
    console.log("progress begin " + (label == undefined ? ":" : label + ":"), nesting);
    var $container;
    var $progress;

    $container = $('div#progressbar');
    $progress = $('.progressfg', $container);

    if (nesting == 0) {
        $container.css('visibility', 'visible');
        $progress.animate({width: $container.width() / 2}, 500.0, 'linear', function() {
            $container.css('visibility', 'visible');
        });
    }

    nesting = nesting + 1;

    return function() {
        if (nesting > 0) {
            nesting -= 1;
            if (nesting == 0) {
                $container.css('visibility', 'visible');
                $progress.animate({width: $container.width()}, 500.0, 'linear', function() {
                    $progress.css({width: 0});
                    $container.css('visibility', 'hidden');

                    $progress = null;
                });
            }
        }
    }
}

function keys(obj) {
    ret = [];
    for (var k in obj) {
        ret.push(k);
    }
    return ret;
}

function arrayRemove(a, e) {
    for (var k in a) {
        if (a[k] == e) {
            a.splice(k, 1);
            break;
        }
    }
}

function arrayFindIndex(a, e) {
    for (var k in a) {
        if (a[k] == e) {
            return k
        }
    }
}

function objDump(arg) {
    var ret = "" + typeof(arg) + "{\n";
    ret += keys(arg).join(",\n");
    ret = ret + "\n}";
    return ret;
}

function empty(obj) {
    for (var k in obj) {
        return false;
    }
    return true;
}

function twoDigit(val) {
    if (val < 0) return '00';
    val = val % 100;
    if (val < 10) return '0' + Math.floor(val);
    return Math.floor(val);
}

function strftime(fmt, date) {
    var keys = {
        '%a': ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'][date.getDay()],
        '%d': twoDigit(date.getDate()),
        '%e': date.getDate(),
        '%b': ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'][date.getMonth()],
        '%m': twoDigit(date.getMonth()),
        '%y': twoDigit(date.getFullYear()),
        '%Y': date.getFullYear(),
        '%H': twoDigit(date.getHours()),
        '%M': twoDigit(date.getMinutes()),
        '%S': twoDigit(date.getSeconds())
    };
    for (var k in keys) {
        fmt = fmt.replace(new RegExp(k, 'g'), keys[k]);
    }
    return fmt;
}


function loadSettings(scope, filter, success, error) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = guard(function() {
        if (xhr.readyState == 4) {
            var json = JSON.parse(xhr.responseText || "{}");
            if (xhr.status > 299) {
                console.log('loadSettings error ' + xhr.status + ' ' + xhr.statusText);
                if (error) {
                    error(json);
                }
                return;
            }
            success(json);
        }
    });
    xhr.open('GET', "/?s=" + encodeURIComponent(scope) + "&sk=" +
        encodeURIComponent(filter), true);
    xhr.setRequestHeader("Content-Type", "application/json");
    xhr.send();
}

function zoomOutIntervals() {
    theGrid.$self.find('canvas').css('opacity', 0.5);
    restoreOriginalInterval();
    calcReloadInterval();
    refresh();
 }

function Widget(obj, $obj, owner) {
    var self = this;
    this.$obj = $obj;
    obj.$self = $obj;
    obj.widget = this;
    this.obj = obj;
    this.children = [];
    this.owner = owner;
    if (owner) {
        owner.children.push(this);
    }
    obj.$self.mousedown(guard(function(ev) {
        self.objForward(ev, 'mousedown');
    }));
    obj.$self.mouseup(guard(function(ev) {
        self.objForward(ev, 'mouseup');
    }));
    obj.$self.mousemove(guard(function(ev) {
        self.objForward(ev, 'mousemove');
    }));
    obj.$self.click(guard(function(ev) {
        self.objForward(ev, 'click');
    }));
}
Widget.prototype.objForward = guard(function Widget_objForward(ev, name) {
    if (this.obj[name]) {
        this.obj[name](ev);
    }
})
Widget.prototype.actualWidth = function Widget_actualWidth() {
    return this.$obj.outerWidth(true);
}

//  GraphSurface turns a $div ($self) into a graph destination
//  parWig is the widget of the parent container
function GraphSurface($self, parWig) {
    //  not a widget?
    if (!parWig.$obj) {
        errorDialog("GraphSurface parent is not a widget:\n" + objDump(parWig));
        return;
    }
    var self = this;
    $self.css({position: 'relative'});
    this.$self = $self;
    this._series = {};
    this._lastRenderData = {};
    this._dygraph = null;
    this._format = 'customBars';
    $('span.closebox', $self).click(guard(function() {
        self.close();
    }));
    $('span.zoomoutbox', $self).click(guard(function() {
        zoomOutIntervals();
    }));
    $('span.settingsbox', $self).click(guard(function() {
        self.showSettings();
    }));
    $('span.summarybox', $self).click(guard(function() {
        self.toggleSummary();
    }));
    new Widget(this, $self, parWig);
    theGrid.add(this);
}
GraphSurface.prototype._destroyDygraph = function GraphSurface_destroyDygraph() {
    if(this._dygraph) {
        this._dygraph.destroy();
        this._dygraph = null;
    }
}
GraphSurface.prototype.toggleSeries = guard(function GraphSurface_toggleSeries(path) {
    if (this._series[path]) {
        // do not allow the last series to be deleted
        if (keys(this._series).length > 1) {
            delete this._series[path];
        }
    } else {
        this._series[path] = function(s) { return s };
    }
    this.reload();
})
GraphSurface.prototype.click = guard(function GraphSurface_click(ev) {
    ev.stopPropagation();
    theGrid.select(this);
})
GraphSurface.prototype.reload = function GraphSurface_reload() {
    var self = this;
    var seriesKeys = keys(this._series);
    theLoader.addToNextGet(seriesKeys, 500,
        theGraphMaxSamples ? theGraphMaxSamples : theGraphSize.width,
        function(data) {
            self.renderData(seriesKeys, data);
        }
    );
}

GraphSurface.prototype.renderData = function GraphSurface_renderData(seriesKeys, data) {
    if (seriesKeys.length == 0) {
        //  removed -- don't do more
        console.log('seriesKeys is empty -- not rendering');
        return;
    }
    this._lastRenderData = {start: data.start, stop:data.stop, interval:data.interval};
    for (var i = 0; i < seriesKeys.length; i++) {
        var k = seriesKeys[i]
        this._lastRenderData[k] = data[k];
    }
    var self = this;
    //  asynchronize actual painting
    setTimeout(function() {
        if (!pageIsHidden()) {
            self.repaint();
        }
    }, 1);
}

GraphSurface.prototype.getTextWidth = function(text) {
    if (!this.context) {
        this.context = document.createElement("canvas").getContext("2d");
        this.context.font = "16px sans-serif";
    }
    return Math.ceil(this.context.measureText(text).width);
}

GraphSurface.prototype.repaint = guard(function GraphSurface_repaint() {
    var $div = $('.graphdiv', this.$self);
    $div.css({"margin-left": 0, "margin-top": 10, "background": "transparent"});
    $div.width(theGraphSize.width - 30);
    $div.height(theGraphSize.height - 60);

    // about to do a bad thing with the plot array.  assuming data
    // arrays are the same length and has same start time and interval
    var data = this._lastRenderData;
    var plotTimes = [];
    var labels = ['time'];

    var interval = data.interval;
    var start = data.start;
    var stop = data.stop;
    var format = this._format || 'noBars';

    // initialize data array with Date objects
    var i = start;
    while ( i < stop ) {
        plotTimes.push([new Date(i*1000)]);
        i += interval;
    }
    if(plotTimes.length == 0) {
        return;
    }


    var minimum = Math.pow(2,100) - 1; // pick a really big minimum to start
    var maximum = -minimum;
    var minVal = minimum;
    var gotdata = false;
    var pushfn = null;
    var ann_maxval = maximum;
    var ann_maxtime = null;
    var ann_minval = minimum;
    var ann_mintime = null;

    //  "errorBars" really means sdev
    //  "customBars" really means min/max
    //  "noBars" means no bars :-)
    if (format == 'noBars' || format == 'stacked' || format == 'area') {
        pushfn = function(plot, bucket) {
            if (!bucket) {
                plot.push(NaN);
            }
            else {
                plot.push(bucket.avg);
                minimum = Math.min(bucket.avg, minimum);
                maximum = Math.max(bucket.avg, maximum);
                minVal = Math.min(bucket.avg, minVal);
                gotdata = true;
            }
        };
    }
    else if (format == 'errorBars') {
        pushfn = function(plot, bucket) {
            if (!bucket) {
                plot.push([NaN, NaN]);
            }
            else {
                plot.push([bucket.avg, bucket.sdev]);
                minimum = Math.min(bucket.avg-bucket.sdev, minimum);
                maximum = Math.max(bucket.avg+bucket.sdev, maximum);
                minVal = Math.min(bucket.avg, minVal);
                gotdata = true;
            }
        };
    }
    else {
        pushfn = function(plot, bucket) {
            if (!bucket) {
                plot.push([NaN, NaN, NaN]);
            }
            else {
                plot.push([bucket.min, bucket.avg, bucket.max]);
                minimum = Math.min(bucket.min, minimum);
                maximum = Math.max(bucket.max, maximum);
                minVal = Math.min(bucket.min, minVal);
                gotdata = true;
            }
        };
    }
    var annotations = [];
    jQuery.each(data, function(key,value) {
        var buckets = data[key]['data'];
        if (buckets) {
            var ann_min = Math.pow(2, 100);
            var ann_max = -ann_min;
            var ann_min_ts = null;
            var ann_max_ts = null;
            var bidx = 0;
            jQuery.each(plotTimes, function(i, plot) {
                // get next bucket of data to insert
                while ((bidx < buckets.length) && (buckets[bidx].time == 0)) {
                    bidx++;
                }

                if (bidx >= buckets.length) {
                    pushfn(plot, null);
                }
                else {
                    var bucket = buckets[bidx];
                    var btime = bucket.time*1000;
                    var timestamp = plot[0].getTime();
                    if (bucket.min < ann_min) {
                        ann_min = bucket.min;
                        ann_min_ts = timestamp;
                    }
                    if (bucket.max > ann_max) {
                        ann_max = bucket.max;
                        ann_max_ts = timestamp;
                    }

                    if (btime == timestamp) {
                        pushfn(plot, bucket);
                        bidx += 1;
                    }
                    else {
                        pushfn(plot, null);
                    }
                }
            });
            labels.push(key);
            annotations.push({
                series: key,
                x: ann_min_ts,
                width: GraphSurface.prototype.getTextWidth(ann_min.toString()) + 2,
                height: 14,
                shortText: ann_min.toString(),
                text: key + " min " + ann_min.toString()
            });
            annotations.push({
                series: key,
                x: ann_max_ts,
                width: GraphSurface.prototype.getTextWidth(ann_max.toString()) + 2,
                height: 14,
                shortText: ann_max.toString(),
                text: key + " max " + ann_max.toString()
            });
            ann_minval = Math.min(ann_min, ann_minval);
            if (ann_minval == ann_min) {
                ann_mintime = ann_min_ts;
            }
            ann_maxval = Math.max(ann_max, ann_maxval);
            if (ann_maxval == ann_max) {
                ann_maxtime = ann_max_ts;
            }
        }
    });

    // for stacked charts, the y axis must be based on max of the sum
    // of the y values for each x.
    if (format == 'stacked') {
        var sums = Array(plotTimes.length);
        jQuery.each(data, function(key, series) {
            if (series instanceof Object && 'data' in series) {
                jQuery.each(series['data'], function(index, bucket) {
                    if (sums[index] == undefined) {
                        sums[index] = bucket.avg;
                    }
                    else {    
                        sums[index] += bucket.avg;
                    }
                });
            }
        });
        maximum = -Math.pow(2,100) - 1; // pick a really big negative maximum to start
        jQuery.each(sums, function(key, value) {
            maximum = Math.max(value, maximum);
        });
        
    }

    // Doing Dygraph's job of calculating the data range properly,
    // so that error bars don't go outside the plotted data set.
    if (!gotdata) {
        // bah!  got no data!
        // make the empty graph show the range from 0-1 so at least we get some y axis labels
        minimum = 0;
        maximum = 1.1;
    }
    else {
        // need to add % above the ends of of range for easy selection of zoom ranges
        range_slop = (maximum - minimum) * 0.05;
        if (range_slop == 0.0) {
            range_slop = 0.5;
        }
        maximum = maximum + (range_slop * 2); // 10% extra on top
        minimum = minimum - range_slop;       // 5% extra on bottom!
    }

    // Does the sdev range go negative, but no value goes negative? Clamp to 0.
    if (minVal >= 0 && minimum < 0) {
        minimum = 0;
    }

    // If we have no interaction model yet, define one, by copying the default dygraph.
    // We need to do this to provide a few extra overrides in spots to dygraph behaviour.
    if(!theInteractionModel) {
        var model = {};
        theInteractionModel = model;

        var defaultModel = Dygraph.Interaction.defaultModel;
        for(k in defaultModel) {
            model[k] = defaultModel[k];
        }

        // The mouse up event tracks the domain of the graph,
        // so we can use that for a zoomCallback that will fire directly after.
        model.lastDomain = {};
        model.mouseup = guard(function(event, g, context) {
            model.lastDomain[g] = g.xAxisRange().slice(0);
            defaultModel.mouseup(event, g, context);
        });

        // Double click to restore zoom *AND* restore original time interval.
        model.dblclick = guard(function(event, g, context) {
            zoomOutIntervals();
            defaultModel.dblclick(event, g, context);
        });
    }

    theGrid.$self.find('canvas').css('opacity', 1.0);

    this._destroyDygraph();
    var params = {
            'valueRange': [minimum, maximum],
            'labelsDiv': $div.parents('.graph').find('.legend').get()[0],
            'legend': 'always',
            'labels': labels,
            'showLabelsOnHighlight': true,
            'zoomCallback': function(minX, maxX, yRanges) {
                // We need to use the interactionModel's stored domain to check if the X axis is changed.
                // Y zooming doesn't change the X axis, so this is reliable.
                var domain = theInteractionModel.lastDomain[g] || [NaN, NaN];

                // If we're zooming on the x, then we need to fetch finer resolution data.
                // Would have used g.isZoomed('x'), but that will always return false,
                // except for the newest Dygraph instance which gets the correct values.
                // This is a cheesy workaround in the meantime!
                if(minX != domain[0] || maxX != domain[1]) {
                    theGrid.$self.find('canvas').css('opacity', 0.5);
                    theCurrentDates.start = new Date(minX);
                    theCurrentDates.stop = new Date(maxX);
                    calcReloadInterval();
                    refresh();
                }
            },
            'interactionModel': theInteractionModel,
            'labelsKMB': true,
            'axes': {
                'x' : {
                    'valueFormatter': function (x) {return ''},
                },
                'y' : {
                    'valueFormatter': function (y) {return ''},
                    'pixelsPerlLabel': 20
                }
            },
        };

    if (labels.length > 2) {
        params.highlightSeriesOpts = {
            highlightCircleSize: 5,
            strokeWidth: 3,
        };
    }

    if (format == 'errorBars') {
        params.errorBars = true;
    }
    else if (format == 'customBars') {
        params.customBars = true;
    }
    else if (format == 'stacked') {
        params.stackedGraph = true;
    }
    else if (format == 'area') {
        params.fillGraph = true;
    }

    var g = this._dygraph = new Dygraph(
        // containing div
        $div[0],
        plotTimes,
        params
    );
    g.setAnnotations(annotations);
    $('.summary', this.$self).html("<span class='nobreak'>Maximum: " + ann_maxval + " at " +
        (new Date(ann_maxtime)).toLocaleDateString() + "</span><span class='nobreak'> Minimum: " + 
        ann_minval + " at " + (new Date(ann_mintime)).toLocaleDateString() + "</span>");
});
GraphSurface.prototype.close = guard(function GraphSurface_close() {
    this._destroyDygraph();
    this._series = null;
    this.$self.remove();
    theGrid.remove(this);
});
GraphSurface.prototype.getSeries = function GraphsSurface_getSeries() {
    var ret = [];
    for (var s in this._series) {
        ret.push(s);
    }
    return ret;
}
GraphSurface.prototype.showSettings = function GraphSurface_showSettings() {
    var self = this;
    choiceDialog(    "Choose display format for these graphs.",
    {
        'noBars': "Lines",
        'errorBars': "StdDev",
        'customBars': "Min/Max",
        'stacked' : "Stacked",
        'area' : "Area"
    },
    function(v) {
        console.log("Selected format: " + v);
        self._format = v;
        self.repaint();
    });
}
GraphSurface.prototype.toggleSummary = function GraphSurface_toggleSummary() {
    $('.summary', this.$self).toggleClass('visible');
}

function DashboardList(id, owner) {
    this.$self = $('#' + id);
    this.$inner = $('.picklist', this.$self);
    this.itemsByName = {};
    this.currentDashboard = name;
    new Widget(this, this.$self, owner);
}
DashboardList.prototype.reload = function DashboardList_reload() {
    console.log('DashboardList.reload() ' + theUserName);
    var self = this;
    $('li', self.$inner).remove();
    if (theUserName) {
        loadSettings(theUserName, 'dashboard.*',
            function(json) { self.on_userDashboards(theUserName, json); },
            function(json) { self.on_userDashboards(theUserName, {}); });
    }
    else {
        self.on_userDashboards(theUserName, {});
    }
}
DashboardList.prototype.setCurrentDashboard = function DashboardList_setCurrentDashboard(name) {
    this.currentDashboard = name;
    this.updateCurrentDashboard();
}
DashboardList.prototype.updateCurrentDashboard = function DashboardList_updateCurrentDashboard() {
    this.$inner.find('li').removeClass('current');
    if(!this.currentDashboard) {
        return;
    }
    var $li = this.itemsByName[this.currentDashboard];
    if($li) {
        $li.addClass('current');
    }
}
DashboardList.prototype.on_userDashboards = function DashboardList_onUserDashboards(context, json) {
    console.log('DashboardList.on_userDashboards(' + context + ')');
    var self = this;
    for (var k in json) {
        var ss = k.substr(0, 10);
        if (ss == 'dashboard.') {
            var $li = $("<li class='pick'></li>");
            var ds = k.substr(10);

            $li.attr('title', ds);
            $('<span class="text"></span>').text(ds).appendTo($li);
            $remove_button = $('<span class="remove_button" title="Remove dashboard"></span>').appendTo($li);
            /*$('<div class="clear"></div>').appendTo($li);*/

            (function() {
                var name = '' + ds;
                $li.click(guard(function() {
                    load_dashboard({dashboard:name}, [context], begin());
                }));
                $remove_button.click(guard(function(ev) {
                    ev.stopPropagation();

                    choiceDialog("Are you sure sure you want to delete '" + name + "'?",
                        {'delete': 'Delete', 'no': 'Cancel'},
                        function(opt) {
                            if (opt == 'delete') {
                                saveSettings(theUserName, 'dashboard.' + name, "", function() {
                                    self.reload();
                                    if(name == self.currentDashboard) {
                                        window.location.href = window.location.href.split('#')[0];
                                    }
                                });
                            }
                        });
                }));
            }
            )();
            self.itemsByName['' + ds] = $li;
            self.$inner.append($li);
        }
    }
    if (context != 'global') {
        self.$inner.append($("<li class='disabled separator'></li>"));
        loadSettings('global', 'dashboard.*',
            function(json) { self.on_userDashboards('global', json); },
            function(json) { self.on_userDashboards('global', {}); });
    }
    self.updateCurrentDashboard();
}

function TabCollection(id) {
    this.$self = $('#' + id);
    new Widget(this, this.$self, null);
    this.$activetab = $('.active.tab', this.$self);
    this.rebuildTabs();
}
TabCollection.prototype.rebuildTabs = function TabCollection_rebuildTabs() {
    var $labels = $('>div.labels', this.$self);
    $('>div', $labels).remove();
    var i = 0;
    $('>div.tab', this.$self).each(function(ix, el) {
        i += 1;
        var $tab = $("<div class='tabtop'></div>");
        var $el = $(el);
        var id = $el.attr('id');
        $tab.text(id.charAt(0).toUpperCase() + id.substring(1));
        $tab.attr('title', $tab.text());

        if($el.hasClass('active')) {
            $tab.addClass('active');
        }
        $labels.append($tab);

        $tab.click(guard(function() {
            $('>div', $labels).removeClass('active');
            $tab.addClass('active');
            $('>div.tab', $el.parent()).removeClass('active');
            $el.addClass('active');
        }));
    });
}

function GraphGrid(id) {
    this._allGraphs = [];
    new Widget(this, $('#' + id), null);
}
GraphGrid.prototype.newGraph = guard(function GraphGrid_newGraph() {
    var $ret = $("<div class='graph'><span title='Show/Hide summary' class='summarybox buttonbox'/><span title='Settings for display' class='settingsbox buttonbox'/><span title='Restore default zoom' class='zoomoutbox buttonbox'/><span title='Close' class='closebox buttonbox'/><div class='legend'/><div class='graphdiv'></div><div class='summary'></div></div>");
    $ret.width(theGraphSize.width);

    this.$self.append($ret);
    var surface = new GraphSurface($ret, theGrid.widget);

    $ret.find('.legend').mouseover(function() {
        $('span', this).each(function(i, elem) {
            $(elem).unbind();
            $(elem).click(guard(function () {
                surface.toggleSeries($(this).contents()[1].wholeText.slice(1))
            }));
        });
    });

    return surface;
})
GraphGrid.prototype.select = guard(function GraphGrid_select(graph) {
    $('.selected', this.$self).removeClass('selected');
    this.selected = graph;
    if (graph) {
        this.selected.$self.addClass('selected');
    }
})
GraphGrid.prototype.click = function GraphGrid_click(ev) {
    $('.selected', this.$self).removeClass('selected');
    this.selected = null;
}
GraphGrid.prototype.repaintAll = function GraphGrid_repaintAll() {
    //  don't lock up the browser for the entire time if
    //  you have lots of graphs.
    var val = 1;
    var self = this;
    for (var k in this._allGraphs) {
        (function() {
            var graph = self._allGraphs[k];
            setTimeout(function() {
                graph.repaint();
            }, val);
        })();
        val = val + 1;
    }
}
GraphGrid.prototype.reloadAll = function GraphGrid_reloadAll() {
    for (var k in this._allGraphs) {
        var graph = this._allGraphs[k];
        graph.reload();
    }
}
GraphGrid.prototype.add = function GraphGrid_add(graph) {
    this._allGraphs.push(graph);
}
GraphGrid.prototype.remove = function GraphGrid_remove(graph) {
    if (graph == this.selected) {
        this.selected = null;
    }
    arrayRemove(this._allGraphs, graph);
    if(this._allGraphs.length == 0) {
        restoreOriginalInterval();
    }
}
GraphGrid.prototype.getDashboard = function GraphGrid_getDashboard() {
    var ret = {
        size: theGraphSize,
        autoRefresh: isAutoRefresh,
        timeInterval: (theCurrentDates.stop.getTime() - theCurrentDates.start.getTime())/1000,
        timeSlider: theTimeSlider.isManual() ? 'manual' : theTimeSlider.getIndex(),
        sizeDropdown: typeof(theSizeDropdownValue) == 'number' ? theSizeDropdownValue : 'manual'
    };
    var graphs = [];
    var formats = [];
    for (var g in this._allGraphs) {
        var graph = this._allGraphs[g];
        graphs.push(graph.getSeries());
        formats.push(bars_fmt_to_ix[graph._format]);
    }
    ret.graphs = graphs;
    ret.formats = formats;
    return ret;
}
GraphGrid.prototype.clear = function GraphGrid_clear() {
    this.selected = null;
    var copy = this._allGraphs.slice(0, this._allGraphs.length);
    //  close all graphs
    for (var k in copy) {
        copy[k].close();
    }
}

function HSplitter(id) {
    var $self = $('#' + id);
    this.$self = $self;

    $self.css('cursor', 'w-resize');
    var move = function(ev) {
        $('body').css('cursor', 'w-resize');

        $(document).mousemove(function(ev) {
            var x = ev.pageX;
            if(x >= 0) {
                $('#lefttab').width(x - 16);
                documentResize();
            }
        });
        $(document).mouseup(function() {
            $('body').css('cursor', '');
            $(document).unbind('mousemove');
            $(document).unbind('mouseup');
        });
    };

    $self.mousedown(move);
    new Widget(this, $self, null);
}

function toggle_lefttab_visibility() {
    var old_element_width = "";
    return function(ev) {
        ev.stopPropagation();
        if ($('#lefttab').hasClass('closed')) {
            $('#lefttab').css("width", old_element_width);
            $('#hsplit').removeClass('closed');
            $('#lefttab').removeClass('closed');
            $('#hide_lefttab').removeClass('closed');
        }
        else {
            old_element_width = $('#lefttab').css('width');
            $('#lefttab').css("width", "0px");
            $('#hsplit').addClass('closed');
            $('#lefttab').addClass('closed');
            $('#hide_lefttab').addClass('closed');
        }
            
        documentResize();
    };
};

function TimeSlider(id) {
    var self = this;
    var $self = $('#' + id);
    this.$self = $self;
    this.manual = false;
    this.$thumb = $self.find('.thumb');
    this.$gauge = $self.find('.gauge');
    this.$value = $self.find('.value');
    this.$marker_holder = $self.find('.markers');
    this.$markers = this.$marker_holder.children('.marker');

    this.width = this.$gauge.width();
    this.snap = this.width / (this.$markers.length - 1);
    this.index = null;
    this.dragging = false;

    this.$marker_holder.attr('unselectable', 'on');
    this.$markers.attr('unselectable', 'on');
    this.$markers.each(function(ix, item) {
        $(item).css('left', ix * self.snap + 2);
    });

    var dfl = $self.attr('default');
    if(dfl) {
        this.setIndex(dfl);
    }

    var move = function(ev) {
        self.move(ev);
    }

    this.$thumb.mousedown(move);
    this.$gauge.mousedown(move);
    this.$markers.mousedown(move);

    new Widget(this, $self, null);
}
TimeSlider.prototype.getIndex = function() {
    return this.index;
}
TimeSlider.prototype.setIndex = guard(function TimeSlider_setIndex(index) {
    index = Math.min(Math.max(index, 0), this.$markers.length - 1);
    this._moveThumb(index);
    this._update(index);
});
TimeSlider.prototype.isManual = function() {
    return this.manual;
}
TimeSlider.prototype.setManual = function(manual) {
    this.$self.toggleClass('manual', manual);
    this.manual = manual;
}
TimeSlider.prototype._moveThumb = function TimeSlider_moveThumb(index) {
    var x = index * this.snap + 4;
    this.$value.width(x + 'px');
    this.$thumb.css('left', x + 'px');
    this.setManual(false);
};
TimeSlider.prototype._update = function TimeSlider_updateIndex(index) {
    if(index != this.index) {
        var txt = this.$markers.eq(index).attr('action');
        eval(txt);
    }
    this.index = index;
}
TimeSlider.prototype._calculateIndex = function TimeSlider_updateIndex(x) {
    var index = Math.round((x - this.$gauge.offset().left) / this.snap);
    return Math.min(Math.max(index, 0), this.$markers.length - 1);
}
TimeSlider.prototype.move = function TimeSlider_move(ev) {
    this._moveThumb(this._calculateIndex(ev.pageX));
    $('body').css('cursor', 'pointer');

    if(!this.dragging) {
        this.dragging = true;

        var self = this;
        $(document).mousemove(function(ev) {
            self.move(ev);
        });
        $(document).mouseup(guard(function(ev) {
            $(document).unbind('mousemove');
            $(document).unbind('mouseup');

            $('body').css('cursor', '');
            self.dragging = false;

            self._update(self._calculateIndex(ev.pageX));
        }));
    }
}

//  The idea of the Loader is that we batch a bunch of counter gets
//  into a single request, for better throughput.
function Loader() {
    this._nextEvent = null;
    this._nextEventTime = 0;
    this._getting = {};
    this._cbObjs = {};
    this._xhr = null;
    this._id = 0;
}
Loader.prototype.addToNextGet = guard(function Loader_addToNextGet(keys, maxTime, maxSamples, cb) {
    this._id = this._id + 1;
    if (this._id > 1000000) {
        //  recycle IDs at some point
        this._id = 0;
    }
    var now = getAdjustedNow();
    var deadline = now.getTime() + maxTime;
    var cbObj = { keys: keys, cb: cb, id: this._id, maxSamples: maxSamples };
    this._cbObjs[cbObj.id] = cbObj;
    for (var k in keys) {
        var key = keys[k];
        if (!this._getting[key]) {
            this._getting[key] = [];
        }
        this._getting[key].push(cbObj);
    }
    if (this._nextEventTime > deadline) {
        clearTimeout(this._nextEvent);
        this._nextEvent = null;
        this._nextEventTime = 0;
    }
    if (this._nextEvent == null) {
        var self = this;
        this._nextEventTime = deadline;
        this._nextEvent = setTimeout(function() {
            self.onTimer();
        }, deadline - now.getTime());
    }
})
Loader.prototype.onTimer = guard(function Loader_onTimer() {
    this._nextEvent = null;
    this._nextEventTime = 0;
    if (this._xhr) {
        //  I'll do this when _xhr completes
        return;
    }
    this.startRequest();
})
Loader.prototype.startRequest = guard(function Loader_startRequest() {
    if (this._xhr) {
        throw new Error("Attempt to re-start Loader XMLHttpRequest()");
        return;
    }
    if (empty(this._getting)) {
        //  nothing to get -- we're done!
        return;
    }
    this._xhr = new XMLHttpRequest();
    var self = this;
    var gotten = this._getting;
    this._getting = {}
    var cbObjs = this._cbObjs;
    this._cbObjs = {}
    this._xhr.onreadystatechange = function () {
        if (self._xhr.readyState == 4) {
            self.onXhrComplete(gotten, cbObjs);
        }
    }
    var maxSamples = 1;
    for (var k in cbObjs) {
        if (cbObjs[k].maxSamples > maxSamples) {
            maxSamples = cbObjs[k].maxSamples;
        }
    }
    //  should we make this custom method MGET?
    this._xhr.open("POST", "/*", true);
    this._xhr.setRequestHeader('Content-Type', 'application/json');
    var req = { start: Math.floor(theCurrentDates.start.getTime() / 1000),
        stop: Math.floor(theCurrentDates.stop.getTime() / 1000),
        keys: keys(gotten),
        maxSamples: maxSamples
        };
    var reqstr = JSON.stringify(req);
    //  for debugging, pend the sending
    self._xhr.send(reqstr);
})
Loader.prototype.onXhrComplete = guard(function Loader_onXhrComplete(gotten, cbObjs) {
    var xhr = this._xhr;
    this._xhr = null;
    if (xhr.status > 299) {
        throw new Error("Error getting data from server:\n" + xhr.status + " " + xhr.statusText);
    }
    else {
        var data = JSON.parse(xhr.responseText);
        this.deliverData(data, gotten, cbObjs);
    }
    //  try reading whatever got queued next
    this.startRequest();
})
Loader.prototype.deliverData = function Loader_deliverData(data, gotten, cbObjs) {
    for (var k in cbObjs) {
        cbObjs[k].cb(data);
    }
}


function toggleGraphing(path, graph_offset)
{
    if (typeof(graph_offset) == 'undefined') {
        if (!theGrid.selected)
        {
            var nu = theGrid.newGraph();
            theGrid.select(nu);
        }
        graph = theGrid.selected;
    } else {
        graph = theGrid._allGraphs[graph_offset];
    }

    graph.toggleSeries(path);
}

var documentResize = guard(function() {
    var width = $(window).width();
    var cw = theTabs.widget.actualWidth();
    cw += theSplitter.widget.actualWidth();
    width -= cw;
    width -= 1;
    theGrid.$self.css({position: 'absolute', width: width, left: cw});
    var height = $(window).height() - 5;
    theTabs.$self.height(height);
    theTabs.$self.find('.tab').each(function(el) {
        $(this).height(height - $(this).offset().top);
    })
    theTabs.$self.find('.tab .scroll').each(function(el) {
        $(this).height(height - $(this).offset().top);
    })

    theGrid.$self.height(height);

    theSplitter.$self.height(height);
    theGrid.repaintAll();
})

var saveSettings = guard(function _saveSettings(scope, name, value, cb) {
    var done = begin();
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = guard(function _saveSettings_readyStateChange() {
        if (xhr.readyState == 4) {
            done();
            if (xhr.status > 299) {
                errorDialog("saveSettings error: " + xhr.status + " " + xhr.statusText +
                    "\n" + xhr.responseText);
                if (cb) {
                    cb();
                }
                return;
            }
            var json = JSON.parse(xhr.responseText);
            if (!json.success) {
                errorDialog("saveSettings error: \n" + json.message);
                if (cb) {
                    cb();
                }
                return;
            }
            if (cb) {
                cb();
            }
        }
    });
    xhr.open('POST', '/?s=' + escape(scope), true);
    xhr.setRequestHeader("Content-Type", "application/json");
    var obj = {};
    if(value === '')
    {
        obj[name] = '';
    }
    else
    {
        obj[name] = JSON.stringify(value);
    }
    xhr.send(JSON.stringify(obj));
});

var theInteractionModel = null;
var theUser = null;
var theUserName = null;
var theTabs = null;
var theCounters = null;
var theGrid = null;
var theDashboards = null;
var theSplitter = null;
var theLoader = null;
var theTimeSlider = null;
var theSizeDropdownValue = null;

var tmpDate = getAdjustedNow();
var theOriginalDates = { 'start': new Date(tmpDate.getTime() - 3600*1000), 'stop': tmpDate };
var theCurrentDates = { 'start': theOriginalDates.start, 'stop': theOriginalDates.stop };
var theGraphSize = { width: 600, height: 240 };
var theGraphMaxSamples = null;
var theReloadInterval = 10000;

function setGraphSize(sz)
{
    theGraphSize = sz;
    $('#grid .graph').width(sz.width);
    $('#grid .graphdiv').css(sz);
    documentResize();
}

function set_interval(i) {
    theOriginalDates.start = new Date(theOriginalDates.stop.getTime() - i * 1000);
    theGraphMaxSamples = undefined;
    restoreOriginalInterval();
    calcReloadInterval();
    refresh();
}

function graph_size(w, h, $item) {
    $('.tdropdown.chart_size .tmenuitem').removeClass('current');
    theSizeDropdownValue = null;
    if($item) {
        $item.addClass('current');
        theSizeDropdownValue = $item.parent().children().index($item);
    }
    setGraphSize({width: w, height: h});
}

function size_setCustom(sizeStr)
{
    var a = sizeStr.split('x', 2);
    if (a.length != 2) {
        a = sizeStr.split(',');
    }
    if (a.length != 2) {
        errorDialog("Please specify size as WxH (or W,H).");
        return false;
    }
    var wStr = a[0].replace(/[^0-9]*/g, '');
    var hStr = a[1].replace(/[^0-9]*/g, '');
    if (wStr == '' || hStr == '') {
        errorDialog("Please specify integer width and height as WxH (or W,H).");
        return false;
    }
    var w = parseInt(wStr);
    var h = parseInt(hStr);
    if (w < 10 || w > 4000 || h < 10 || h > 2000) {
        errorDialog("Please specify width and height within reasonable ranges.");
        return false;
    }
    setGraphSize({width: w, height: h});
    $('.tdropdown.chart_size .tmenuitem').removeClass('current');
    return true;
}

function size_custom()
{
    promptDialog('Please enter a graph size: (WxH)', size_setCustom);
}

function change_counters(from_to, keep_old)
{
    var keep = 0;
    if (typeof(keep_old) != 'undefined') {
        keep = keep_old
    }

    var ft = from_to.split(":",2);
    for (var k in theGrid._allGraphs) {
        graph = theGrid._allGraphs[k]
        for (var path in graph._series) {
            if (path.indexOf(ft[0]) >= 0) {
                var new_path = path.replace(ft[0], ft[1]);
                graph.toggleSeries(new_path);
                if (keep == 0) {
                    graph.toggleSeries(path);
                }
            }
        }
        graph.reload();
    }
}

function add_counters(match_add)
{
    change_counters(match_add, 1);
}

function regexp_change()
{
    promptDialog('Match counters, and replace them with another (can be substring).\n[Syntax: match:substitution]', change_counters);
}

function regexp_add()
{
    promptDialog('Match counters, and add their substitution to same graphs (can be substring).\n[Syntax: match:substitution]', add_counters);
}

function save_dashboard_named(name)
{
    var fn = name;
    console.log('save_dashboard(' + fn + ')');
    if (!fn || fn.match(/[^a-zA-Z_0-9]/)) {
        errorDialog("Please enter a proper file name first. Alphanumeric and underscore characters only.");
        return;
    }
    var theDash = theGrid.getDashboard();
    if (theUserName) {
        var save = function() {
            saveSettings(theUserName, 'dashboard.' + fn, theDash, function() {
                theDashboards.reload();
            });
            window.location.hash = '#?dashboard=' + fn;
        }
        if(theDashboards.itemsByName[fn]) {
            choiceDialog("Dashboard named '" + fn + "' already exists. Overwrite it?",
                {'save': 'Save', 'no': 'Cancel'},
                function(opt) {
                    if (opt == 'save') {
                        save();
                    }
                }
            );
        } else {
            save();
        }
    }
    else {
        errorDialog('You must log in to save dashboards');
    }
}

function save_dashboard()
{
    var $div = promptDialog(
        'Save dashboard as: [Valid characters: A-Z, a-z, 0-9, _]',
        save_dashboard_named
    );
    var $answer = $('.answer', $div);
    var dash = theDashboards.currentDashboard;
    if(dash) {
        $answer.val(dash);
    }
    $answer.select();
}

function save_template()
{
    var fn = $('#arg_filename').text();
    if (!fn || fn.match(/[^a-zA-Z_0-9]/)) {
        errorDialog("Please enter a proper file name first. Alphanumeric and underscore characters only.");
        return;
    }
    var theDash = theGrid.getDashboard();
    if (theUser) {
        saveSettings(theUser, 'template.' + fn, theDash);
    }
    else {
        errorDialog('You must log in to save templates');
    }
    window.location.hash = '#?template=' + fn;
}

var isAutoRefresh = false;
var autoRefreshTimer = null;

var refresh = guard(function _refresh() {
    if (pageIsHidden()) {
        return;
    }
    var end = begin('refresh');
    var date = getAdjustedNow();
    console.log('refresh; auto reload interval=' + theReloadInterval + ' date is ' + date);
    var delta = theCurrentDates.stop.getTime() - theCurrentDates.start.getTime();
    if ((theCurrentDates.stop - theCurrentDates.start) == (theOriginalDates.stop - theOriginalDates.start)) {
        theCurrentDates.start = new Date(date.getTime() - delta);
        theCurrentDates.stop = date;
    }
    theGrid.reloadAll();
    end();
});

function setAutoRefresh(ref) {
    console.log('setAutoRefresh interval=' + theReloadInterval);
    if (autoRefreshTimer) {
        clearInterval(autoRefreshTimer);
        autoRefreshTimer = null;
    }
    isAutoRefresh = ref;
    if (isAutoRefresh) {
        $('.tbutton.auto_refresh').addClass('toggle');
        autoRefreshTimer = setInterval(refresh, theReloadInterval*1000);
    }
    else {
        $('.tbutton.auto_refresh').removeClass('toggle');
    }
}

function restoreOriginalInterval() {
    theCurrentDates.start = theOriginalDates.start;
    theCurrentDates.stop = theOriginalDates.stop;
}

function calcReloadInterval() {
    var delta = (theCurrentDates.stop.getTime() - theCurrentDates.start.getTime())/1000;
    var ival = Math.ceil(delta / 500);
    var points = [5, 10, 15, 20, 30, 60]
    for (var k in points) {
        if (points[k] >= ival) {
            theReloadInterval = points[k];
            return;
        }
    }
    theReloadInterval = 120;
}

function auto_refresh() {
    if (isAutoRefresh) {
        setAutoRefresh(0);
    }
    else {
        calcReloadInterval();
        setAutoRefresh(true);
    }
}

//  Also look at load_hash(), the other way to get state in.
function openDashboard(scope, dashboard, json) {
    json = JSON.parse(json["dashboard." + dashboard]);
    $('input#arg_filename').val(dashboard);
    theGrid.clear();
    if (json.size) {
        var $item = null;
        if(json.timeSlider == 'manual') {
            $item = null;
        } else if(typeof(json.sizeDropdown) == 'number') {
            $item = $('.tdropdown.chart_size .tmenuitem').eq(json.sizeDropdown);
        }
        graph_size(json.size.width, json.size.height, $item);
    }
    console.log(json);
    var graphs = json.graphs;
    for (var k in graphs) {
        var nu = theGrid.newGraph();
        if (json.formats) {
            nu._format = bars_ix_to_fmt[json.formats[k]] || "errorBars";
        }
        else {
            nu._format = "errorBars";
        }
        var serii = graphs[k];
        for (var i in serii) {
            var ser = serii[i];
            nu.toggleSeries(ser);
        }
    }
    if (json.timeInterval) {
        theOriginalDates.start = new Date(theOriginalDates.stop.getTime() - json.timeInterval * 1000);
        restoreOriginalInterval();
    }
    if(json.timeSlider == 'manual') {
        theTimeSlider.setManual(true);
    } else if(typeof(json.timeSlider) == 'number') {
        theTimeSlider.setIndex(json.timeSlider);
    }
    calcReloadInterval();
    setAutoRefresh(json.autoRefresh);
    documentResize();

    window.location.hash = '#?dashboard=' + dashboard;
    theDashboards.setCurrentDashboard(dashboard);
}

function load_dashboard(args, contexts, cb) {
    if (!contexts || !contexts.length) {
        errorDialog('Error loading dashboard');
        cb();
        return;
    }
    var ctx = contexts.shift();
    console.log('load_dashboard ' + ctx + ' ' + JSON.stringify(args));
    loadSettings(ctx, 'dashboard.' + args.dashboard,
        function(json) {
            openDashboard(ctx, args.dashboard, json);
            cb();
        },
        function(e) {
            load_dashboard(args, contexts, cb);
        });
}


var bars_ix_to_fmt = {
    1: 'noBars',
    2: 'errorBars',
    3: 'customBars',
    4: 'stacked',
    5: 'area'
};

var bars_fmt_to_ix = {
    'noBars': 1,
    'errorBars': 2,
    'customBars': 3,
    'stacked': 4,
    'area': 5
};

function load_state(state, _ctx, cb) {
    //  clear windows
    theGrid.clear();
    //  load counter sets
    var graphs = state.graphs.split(';');
    for (var k in graphs) {
        var vals = graphs[k].split(',');
        var graph_ix = vals[0];
        var fmt = bars_ix_to_fmt[vals[1]] || "errorBars";
        var nu = theGrid.newGraph();
        nu._format = fmt;
        var serii = state[graph_ix].split(';');
        for (var i in serii) {
            var ser = serii[i];
            nu.toggleSeries(ser);
        }
    }
    //  set time range
    resetDateRange(new Date(parseInt(state['from'])*1000), new Date(parseInt(state['to'])*1000));
    //  I'm done!
    cb();
}

//  Also look at openDashboard(), the other way to get state in.
var load_hash = guard(function _load_hash(hash) {
    var keyvals = hash.split('&');
    var func = null;
    var args = {};
    for (var ki in keyvals) {
        var kv = keyvals[ki].split('=', 2);
        var key = kv[0];
        var value = kv[1];
        if (key == 'dashboard') {
            func = load_dashboard;
        }
        if (key == 'state') {
            func = load_state;
        }
        args[key] = unescape(value);
    }
    var contexts = ['global'];
    if (theUserName) {
        contexts.push(theUserName);
    }
    if (func) {
        var end = begin();
        func(args, contexts, end);
    }
});

function maybeParseCookie(str, cb) {
    console.log('maybeParseCookie', str);
    var cookies = str.split(';');

    for(var i = 0; i < cookies.length; i++) {
        var cookie = cookies[i].split('=');
        if(cookie[0].match(/^\s*login\s*$/)) {
            var login = decodeURIComponent(cookie[1]).split(':');
            doLogin(login[0], null, login[1], cb);
            return;
        }
    }

    $('#login').slideDown();
    cb();
}

function gotohash() {
    var hash = window.location.hash;
    if (hash && hash.substr(0, 2) == '#?') {
        console.log('loading window.location.hash ' + hash);
        load_hash(hash.substr(2));
    }
    documentResize();
}

function packup_state_as_hash() {
    var state = {};
    var theDash = theGrid.getDashboard();
    var graphs = [];
    for (var k in theDash.graphs) {
        var sers = theDash.graphs[k].join(';');
        state[k] = sers;
        var grix = '' + k + ',' + theDash.formats[k];
        graphs.push(grix);
    }
    state.graphs = graphs.join(';');
    var qstr = '#?';
    for (var k in state) {
        qstr += escape(k) + '=' + escape(state[k]) + '&';
    }
    qstr += 'from=' + escape(Math.floor(theCurrentDates.start.getTime()/1000)) + '&';
    qstr += 'to=' + escape(Math.floor(theCurrentDates.stop.getTime()/1000)) + '&';
    qstr += 'state=';
    document.location.hash = qstr;
}

/*
 * work in progress. migrating to the use of backbone, underscorejs and jquery-cookie
 */

var CounterTreeModel = Backbone.Model.extend({
    initialize: function() {
        this.fetch();
    },

    defaults: {
        "pattern": "*",
        "matching_names": []
    },

    url: function() {
        return "/?q=" + this.get("pattern");
    },
});

function asCounterTree(counters) {
    function newNode(text, children, type) {
        return {
            'text':     _.isUndefined(text)     ? '#' : text,
            'children': _.isUndefined(children) ? {}  : children,
            'type':     _.isUndefined(type)     ? 2   : type
        }
    }

    // convert list of counters into a tree
    var parsed_counters = newNode();
    _.each(counters, function(counter, index) {
        var fields = counter.name.split(".");
        var node = parsed_counters;
        _.each(fields, function(field, index) {
            if (!node.children[field]) {
                node.children[field] = newNode(field);
            }    
            node = node.children[field];
        })
        // store counter type in lowest descendant
        node.type = counter.type;
    })

    // collapse nodes that have only 1 child
    function maybeMoveUp(subtree, key, tree) {
        collapse(subtree);
        if (_.size(subtree.children) == 1) {
            var child_key  = _.keys(subtree.children)[0];
            var child_node = subtree.children[child_key];
            var new_key    = key + '.' + child_key;

            tree[new_key] = newNode(new_key, child_node.children, child_node.type)

            delete tree[key];
        }
    }

    function collapse(counters) {
        _.each(counters.children, maybeMoveUp);
    }

    collapse(parsed_counters);

    return parsed_counters;
}

function formatTreeLevel(tree) {
    var icons = {
        0 : 'istatd-gauge',
        1 : 'istatd-counter',
    };

    return _.map(tree.children, function (subtree, key, tree) {
        if (_.size(subtree.children) > 0) {
            return {text: key, children: true};
        }
        else {
            return {text: key, a_attr : {'class': icons[subtree.type]}};
        }
    });
}

var CounterTreeFilterView = Backbone.View.extend({
    initialize: function() {
        var div = $("<div id='counter_filter' />");
        this.icon = $("<div class='magnifying_glass' />");
        this.label = $("<div id='counter_filter_label'>Search</div>");
        this.input = $("<input type='text' id='counter_filter_text' value=''/>");
        this.clear = $("<div id='counter_filter_clear' />");

        div.append(this.label);
        div.append(this.input);
        div.append(this.icon);
        div.append(this.clear);
        this.$el.append(div);
    },

    events: {
        "click #counter_filter_label"  : "label_click",
        "focus #counter_filter_text"   : "input_focus",
        "blur #counter_filter_text"    : "input_blur",
        "keydown #counter_filter_text" : "input_keydown",
        "click #counter_filter_clear"  : "clear_filter",
    },

    label_click: function() {
        this.input.focus();
    },

    input_focus: function() {
        this.label.hide();
    },

    input_blur: function() {
        if (!this.input.val()) {
            this.label.show();
        }
    },

    input_keydown: function(ev) {
        if (ev.which == 13) {
            $('#counter_jstree').jstree("deselect_all");
            this.model.set("pattern", this.input.val());
        }
    },

    clear_filter: function(ev) {
        this.input.val("");
        this.label.show();
        this.model.set("pattern", "");
    }
});

var CounterTreeView = Backbone.View.extend({
    el: '#counters',

    initialize: function() {
        this.counterTreeModel = new CounterTreeModel();

        var tree_filter_view = new CounterTreeFilterView({el: this.$el, model: this.counterTreeModel});

        var scroll_area = $("<div class='scroll' />");
        var jstree_view = $("<div id='counter_jstree' />");

        this.$el.append(scroll_area.append(jstree_view));

        this.listenTo(this.counterTreeModel, 'change', this.render);
                
    },

    render: function() {
        var proto_pat = this.counterTreeModel.get("pattern");

        var pat;
        if (proto_pat == "*") {
            pat = /.*/;
        } 
        else {
            if (proto_pat.indexOf("*") == -1) {
                /* auto wildcard bare words */
                proto_pat = ".*" + proto_pat + ".*";
            }
            else {
                /* convert wildcard characters to regex */
                var subpatterns = proto_pat.split("*");
                proto_pat = subpatterns.join(".*");
            }
            pat = new RegExp(proto_pat);
        }

        // filter counters client side
        var matching_names = this.counterTreeModel.get("matching_names");
        var counters = _.chain(matching_names)
            .filter(function(counter) { return (counter.is_leaf && (counter.name.search(pat) != -1)) })
            .sortBy("name")
            .value();

        var counter_tree = asCounterTree(counters);

        function findNodeInCounterTree(counter_tree, id, parents) {

            // return the node of our counter tree that is associated with the jstree
            // node that was just opened.
            //
            // id is the DOM id of the opened node.  parents is the list of DOM ids
            // from the opened node back up the tree to the root.  We don't need
            // the jstree '#' id for the root of the tree, so we discard it with
            // slice()

            var path = _.clone(parents).reverse().concat(id).slice(1);
            var node = counter_tree;
            _.each(path, function(id) {
                node = node.children[$('#counter_jstree').jstree('get_node', id).text];
            });
            return node;
        };

        function getCounterName(id, parents) {
            var path = _.clone(parents).reverse().concat(id).slice(1);
            return _.map(path, function(id) {
                return $('#counter_jstree').jstree('get_node', id).text;
            }).join('.');
        }

        // function call order matters here.  you must refresh jstree before you destroy
        $('#counter_jstree').jstree('refresh').jstree('destroy');

        $('#counter_jstree')
            .on('select_node.jstree', function (e, data) {
                if ("class" in data.node.a_attr) {
                    var counter = getCounterName(data.node.id, data.node.parents);
                    toggleGraphing(counter);
                }
                else {
                    if ($('#counter_jstree').jstree('is_open', data.node)) {
                        $('#counter_jstree').jstree('close_node', data.node);
                    }
                    else {
                        $('#counter_jstree').jstree('open_node', data.node);
                    }
                }
             })
            .jstree({
                'core': {
                    'data': function(node, cb) {
                        if (node.id == '#') {
                            cb.call(this, formatTreeLevel(counter_tree));
                        }
                        else {
                            var subtree = findNodeInCounterTree(counter_tree, node.id, node.parents);
                            cb.call(this, formatTreeLevel(subtree));
                        }
                    }
                },
                'plugins' : [ "sort" ]
            });
        documentResize();
        return this;
    }

});

var UserModel = Backbone.Model.extend({
    toJSON: function(options) {
        var data = {};
        data["user." + this.get('username')] = JSON.stringify({password: this.get('password_hash')});
        return data;
    }
});

var UsersModel = Backbone.Collection.extend({
    model: UserModel,
    url: "/?s=users",
    parse: function(response, options) {
        return _.map(response, function(value, key) {
            return {
                username: key.split('.')[1],
                password_hash: JSON.parse(value)['password']
            }
        });
    }
});

var SettingsModel = Backbone.Model.extend({
    initialize: function() {
        var self = this;
        this.users = new UsersModel();
        this.users.fetch({
            reset: true,
            success: function() {
               self.trigger("reset");
            }
        });
    },

    getLoggedInUser: function() {
        var login_cookie = $.cookie('login');
        if (!_.isUndefined(login_cookie)) {
            var piece = login_cookie.split(':');
            var user = this.users.findWhere({username: piece[0]});
            if (!_.isUndefined(user)) {
                return user.get('username');
            }
            else {
                return null;
            }
        }
        return null;
    },

    isLoggedIn: function() {
        return !_.isNull(this.getLoggedInUser());
    },

    createUserAndLogin: function(username, password_hash) {
        this.users.create({
            username: username,
            password_hash: password_hash
        });
        this.login(username, password_hash, true);
        //Grrrr this causes istatd to actually save the user to disk
        saveSettings(username, '', '', function () {} );
    },

    /* OK, I know -- getting the hash from the server and checking it client
       side is 100% not secure from the server's point of view. That's not
       the point. The login is only here to prevent casual impersonation, and
       to provide a unique name to store preferences under so different users
       don't step on each other.
       Really, the only reason there's a password is because it's expected ;-)
     */
    login: function(username, password, hashed) {
        var is_hashed = hashed || false;
        var password_hash = is_hashed ? password : Sha256.hash(password + ' salt ' + username);
        var user = this.users.findWhere({username: username});
        var is_new_user = _.isUndefined(user);

        if (is_new_user) {
            var self = this;
            choiceDialog(
                "Could not load user info -- create new user " + username + "?",
                {'create': 'Yes', 'no': 'No'},
                function(opt) {
                    if (opt == 'create') {
                        self.createUserAndLogin(username, password_hash);
                    }
                }
            );
        }
        else {
            //  The fake, client-side password was causing more problems than it solved.
            //  Carry the data forward, but don't enforce it.
            $.cookie('login', username + ':' + password_hash, {expires: 12, path: '/'});
            this.trigger('logged_in', user);
        }

    },

    logout: function() {
        $.removeCookie('login', {expires: 12, path: '/'});
        this.trigger('logged_out');
    },

});

var UserControlsView = Backbone.View.extend({
    el: "#user_controls",
    
    initialize: function() {
        this.model = new SettingsModel();

        this.$login  = $('<div id="login" />');
        this.$logout = $('<div id="logout" class="tdropdown username"></div>');

        this.$el.append(this.$login);
        this.$el.append(this.$logout);

        this.userLoginView  = new UserLoginView({el: this.$login, model: this.model});
        this.userLogoutView = new UserLogoutView({el: this.$logout, model: this.model});

        this.listenTo(this.model, 'reset', function() {
            if (_.isNull(this.model.getLoggedInUser())) {
                this.showViewsWhenNobodyIsLoggedIn();
            }
            else {
                this.showViewsWhenSomebodyIsLoggedIn();
            }
        });

        this.listenTo(this.model, 'logged_in', function() {
            this.showViewsWhenSomebodyIsLoggedIn();
        });

        this.listenTo(this.model, 'logged_out', function() {
            // redirect back myself, this will reset entire UI to logged out state.
            window.location.href = window.location.href.split('#')[0];
        });
    },

    showViewsWhenNobodyIsLoggedIn: function() {
        // nobody is logged in, so show login view
        var self = this;
        this.userLogoutView.slideUp(function() {
            self.userLoginView.slideDown();
        });

        // hmm, this gotohash() still needs to pull into backbone gracefully
        // the call to gotohash() here makes it possible to load bookmark urls 
        // even if a users is not logged into istatd.
        gotohash();
    },

    showViewsWhenSomebodyIsLoggedIn: function() {
        // someone is logged in, so show logout view
        var self = this;
        this.userLoginView.slideUp(function() {
            self.userLogoutView.slideDown();
            // convert to backbone model and view
            theUserName = self.model.getLoggedInUser(); // global needed by old dashboards code.
            $('#save_dashboard').removeClass('disabled');
            theDashboards.reload();

            // hmm, this gotohash() still needs to pull into backbone gracefully
            gotohash();
        });
    },
});

var UserLoginView = Backbone.View.extend({
    initialize: function() {
        this.$username = $('<input type="text" id="loginname" value="Username">'); 
        this.$password = $('<input type="password" id="loginpassword">'); 
        var button     = $('<div class="tbutton" id="loginbutton">Login</div>');
        this.$el.append(this.$username);
        this.$el.append(this.$password);
        this.$el.append(button);
    },
    
    events: {
        "click #loginbutton": "login",
        "keydown #loginname": "input_keydown",
        "keydown #loginpassword": "input_keydown",
        "focus #loginname": "input_focus",
        "focus #loginpassword": "input_focus",
    },

    input_keydown: function(ev) {
        if (ev.which == 13) {
            $('#counter_jstree').jstree("deselect_all");
            if (ev.currentTarget.id == 'loginname') {
                this.$password.focus();
            } else {
                this.$password.blur();
                this.login();
            }
        } 
    },

    input_focus: function(ev) {
        $(ev.currentTarget).val('');
    },

    login: function() {
        this.model.login(this.$username.val(), this.$password.val());
    },

    slideDown: function(callback) {
        this.$el.slideDown();
    },

    slideUp: function(callback) {
        this.$el.slideUp(callback);
    }
});


var istatdDropdownOptions = [ 'items', 'label' ];
var IstatdDropdownView = Backbone.View.extend({
    items: [],
    label: 'no-label',

    constructor: function(options) {
        Backbone.View.apply(this, arguments);
        _.extend(this, _.pick(options, istatdDropdownOptions));
    },

    initialize: function() {},

    renderLabel: function() {
        if (_.isUndefined(this.$label)) {
            this.$label = $('<div class="label"></div>');
            this.$el.append(this.$label);
        }

        this.$label.html(_.result(this, 'label'));
        return this;
    },

    renderDropdown: function() {
        if (_.isUndefined(this.$dropdown)) {
            this.$dropdown = $('<div class="reveal"></div>');
            console.log(this.$dropdown);
            _.each(this.items, function(item_definition) {
                var $entry = $('<div class="tmenuitem">' + item_definition.item + '</div>');
                if (item_definition.selected || false) {
                    $entry.addClass('current');
                }
                this.$dropdown.append($entry);
            }, this);
            this.$el.append(this.$dropdown);
        }

        return this;
    },

    render: function() {
        this
            .renderLabel()
            .renderDropdown()
        return this;
    },

    events: function() {
        var self = this;
        $('body').mouseup(function(ev) {
            self.close(self.$el);
        });

        return {
            'click': 'open',
            'click .reveal .tmenuitem': 'doAction'
        }
    },

    slideDown: function(callback) {
        this.render();
        this.$el.slideDown();
    },

    slideUp: function(callback) {
        this.$el.slideUp(callback);
    },

    open: function(ev) {
        ev.stopPropagation();
        $(ev.delegateTarget).toggleClass('open')
    },

    close: function($el) {
        $el.removeClass('open');
    },

    doAction: function(ev) {
        ev.stopPropagation();
        var item = $(ev.currentTarget).text();
        var item_definition = _.findWhere(this.items, {item: item});
        item_definition['action'](ev, this.model);
    },

    
});

var UserLogoutView = IstatdDropdownView.extend({
    label: function() { return "User: " + this.model.getLoggedInUser(); },

    items: [
        { item: 'Logout', action: function(ev, model) { model.logout(); } }
    ],


});

var ChartSizeView = IstatdDropdownView.extend({
    label: 'Chart Size',

    items: [
        { item: 'Small (300x120)',  action: function(ev) { graph_size(300, 120, $(ev.currentTarget)); } },
        { item: 'Medium (600x240)', action: function(ev) { graph_size(600, 240, $(ev.currentTarget)); }, selected: true },
        { item: 'Large (800x480)',  action: function(ev) { graph_size(800, 480, $(ev.currentTarget)); } },
        { item: 'Custom...',        action: function(ev) { size_custom(); } },
    ],

    initialize: function(options) {
        this.render();
    },
});

var AdvancedToolsView = IstatdDropdownView.extend({
    label: 'Advanced Tools',
    items: [
        { item: 'Regex/Replace...',            action: function(ev) { regexp_change(); } },
        { item: 'Regex/Add...',                action: function(ev) { regexp_add(); } },
        { item: 'Cross-reference Counters...', action: function(ev) { xref_change(); } },
        { item: 'Manual Date Range...',        action: function(ev) { select_daterange(); } },
        { item: 'Create Bookmark URL',         action: function(ev) { packup_state_as_hash(); } },
    ],
    initialize: function(options) {
        this.render();
    },
        
});

var on_ready = guard(function _on_ready() {
    var end = begin('on_ready');
    theTabs = new TabCollection('lefttab');
    theDashboards = new DashboardList('dashboards', theTabs.widget);
    theSplitter = new HSplitter('hsplit');
    theGrid = new GraphGrid('grid');
    theTimeSlider = new TimeSlider('time_slider');

    counterTreeView = new CounterTreeView();
    userControls = new UserControlsView();
    chartSize = new ChartSizeView({el: $('.tdropdown.chart_size')});
    advanceTools = new AdvancedToolsView({el: $('.tdropdown.advanced_tools')});

    $('#hsplit').attr('unselectable', 'on');
    $('.tbutton').attr('unselectable', 'on');
    $('.tdropdown div').attr('unselectable', 'on');
    $('.tslider div').attr('unselectable', 'on');

    $('.tbutton').click(guard(function(ev) {
        if($(this).hasClass('disabled')) {
            return;
        }
        var $src = $(this);
        var txt = $src.attr('action');
        eval(txt);
    }));
    
    $('#hide_lefttab').click(toggle_lefttab_visibility());
        
    $(window).resize(documentResize);
    theLoader = new Loader();

    window.onhashchange = gotohash;

    if(visibilitySupported) {
        document.addEventListener(visibilityChange, function() {
            if(document_hidden != document[hidden]) {
                if(document[hidden]) {
                    refresh();
                }
            }

            document_hidden = document[hidden];
        });
    }
    end();
})
