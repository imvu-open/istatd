
/* A simple in-browser app to browse counters out of istatd. */
/* Copyright 2011 IMVU, Inc. Author: jwatte@imvu.com */
/*

todo:
- arrange colors and counter ordering explicitly
- render modes: area, stacked, line, min/max (in addition to "bands")
*/

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
        tt = (new Date()).getTime();
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

function begin() {
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

function buildHierarchy(items) {
    var ret = {};
    for (var k in items) {
        var item = items[k];
        var pieces = item.name.split('.');
        var top = ret;
        for (var i in pieces) {
            var piece = pieces[i];
            if (!top[piece]) {
                top[piece] = {};
            }
            top = top[piece];
        }
    }
    return ret;
}

function countChildren(obj) {
    var n = 0;
    for (var k in obj) {
        n += 1;
    }
    return n;
}

function createElement(k, path, $parent) {
    var $div = $("<div class='counter'><span class='name'></span><br/></div>");
    $('span.name', $div).text(k);
    $div.attr('title', path);
    $parent.append($div);
    return $div;
}

function buildOneElement(k, hier, builder, $parent, path) {
    var nchildren = countChildren(hier);
    if (nchildren == 1) {
        for (var child in hier) {
            buildOneElement(k+'.'+child, hier[child], builder, $parent, path+'.'+child);
        }
    } else if (nchildren > 1) {
        var $div = createElement(k, path, $parent);
        $div.addClass('branch');
        $div.click(guard(function(ev) {
            ev.stopPropagation();
            $children = $('> div.counter', $div);
            if (!$children.length) {
                var end = begin();
                builder.buildElements($div, hier, path + '.');
                end();
            }
            $div.toggleClass('expanded');
        }));
    } else {
        $div = createElement(k, path, $parent);
        $div.addClass('leaf');
        var bindPath = '' + path;
        $div.click(guard(function(ev) {
            ev.stopPropagation();
            toggleGraphing(bindPath);
        }));
    }
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
        self.repaint();
    }, 1);
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
    var lockatzero = false;

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

    //  "errorBars" really means sdev
    //  "customBars" really means min/max
    //  "noBars" means no bars :-)
    if (format == 'noBars') {
        pushfn = function(plot, bucket) {
            if (!bucket) {
                plot.push([NaN]);
            }
            else {
                plot.push([bucket.avg]);
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
                shortText: ann_min.toString(),
                text: key + " min " + ann_min.toString()
            });
            annotations.push({
                series: key,
                x: ann_max_ts,
                shortText: ann_max.toString(),
                text: key + " max " + ann_max.toString()
            });
        }
    });

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
    /* Does the sdev range go negative, but no value goes negative? Clamp to 0. */
    if (minVal >= 0 && minimum < 0) {
        minimum = 0;
    }
    if (lockatzero) {
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
            'showLabelsOnHighlight': false,
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
            'pixelsPerYLabel': 20
        };
    if (format == 'errorBars') {
        params.errorBars = true;
    }
    else if (format == 'customBars') {
        params.customBars = true;
    }

    var g = this._dygraph = new Dygraph(
        // containing div
        $div[0],
        plotTimes,
        params
    );
    g.setAnnotations(annotations);
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
        'customBars': "Min/Max"
    },
    function(v) {
        console.log("Selected format: " + v);
        self._format = v;
        self.repaint();
    });
}


function CounterHierarchy(id, parWig) {
    new Widget(this, $('#' + id), parWig);
    this.updateXhr = null;
    this.selected = null;
    this.update();
}
CounterHierarchy.prototype.update = guard(function CounterHierarchy_update() {
    var self = this;
    if (!this.updateXhr) {
        var end = begin();
        this.updateXhr = new XMLHttpRequest();
        this.updateXhr.onreadystatechange = function() {
            if (self.updateXhr.readyState == 4) {
                self.updateDone();
                end();
            }
        }
        filt = $('#counter_filter_text').val();
        if ( filt == '' ) {
            filt = "*";
        } else if (filt.indexOf("*") == -1) {
            // auto wildcard bare words
            filt = "*" + filt + "*";
        }
        this.updateXhr.open('GET', '/?q=' + filt);
        this.updateXhr.send();
    }
})
CounterHierarchy.prototype.updateDone = guard(function CounterHierarchy_updateDone() {
    var self = this;
    if (self.updateXhr.status == 200) {
        var data = JSON.parse(self.updateXhr.responseText);
        var hier = buildHierarchy(data.matching_names);
        self.hierarchy = hier;
        var $f = $('#counter_filter', self.$self);
        self.$self.empty();
        var $scroll = $('<div class="scroll">');
        self.$self.append($scroll);
        self.buildElements($scroll, self.hierarchy, '');
        self.$self.prepend($f);
        documentResize();
    }
    else {
        errorDialog("Error getting counter list:\n" + self.updateXhr.status + " " + self.updateXhr.statusText, self.$self);
    }
    self.updateXhr = null;
})

CounterHierarchy.prototype.buildElements = function CounterHierarchy_buildElements($parent, hier, path) {
    var ret = 0;
    var akeys = keys(hier);
    akeys.sort();
    for (var i in akeys) {
        var k = akeys[i];
        buildOneElement(k, hier[k], this, $parent, path + k);
        ret = ret + 1;
    }
    return ret;
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
            $('<div class="text"></div>').text(ds).appendTo($li);
            $remove_button = $('<div class="remove_button" title="Remove dashboard"></div>').appendTo($li);
            $('<div class="clear"></div>').appendTo($li);

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
    var $ret = $("<div class='graph'><span title='Settings for display' class='settingsbox buttonbox'/><span title='Restore default zoom' class='zoomoutbox buttonbox'/><span title='Close' class='closebox buttonbox'/><div class='legend'/><div class='graphdiv'></div></div>");
    $ret.width(theGraphSize.width);

    this.$self.append($ret);
    var surface = new GraphSurface($ret, theGrid.widget);

    $ret.find('.legend').mouseover(function() {
        $('span', this).each(function(i, elem) {
            $(elem).unbind();
            $(elem).click(guard(function () {
                surface.toggleSeries($(this).html().slice(1));
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
    var now = new Date();
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

function setCookie(name, value, extime) {
    console.log('setCookie(' + name + ',' + value + ',' + extime + ')');
    var exdate = new Date();
    exdate = new Date(exdate.getTime() + extime * 1000);
    value = escape(value)
        + ((extime==null) ? "" : "; expires="+exdate.toUTCString())
        + '; path=/';
    document.cookie = name + "=" + value;
}

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

var theOriginalDates = { 'start': new Date((new Date()).getTime() - 3600*1000), 'stop': new Date() };
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

function createUser(name, password, pwHashed, cb) {
    saveSettings('users', 'user.'+name, {
        'password': pwHashed,
    }, cb);
}

/* OK, I know -- getting the hash from the server and checking it client
   side is 100% not secure from the server's point of view. That's not
   the point. The login is only here to prevent casual impersonation, and
   to provide a unique name to store preferences under so different users
   don't step on each other.
   Really, the only reason there's a password is because it's expected ;-)
 */
function doLogin(name, password, pwHashed, cb) {
    var hash = pwHashed || Sha256.hash(password + ' salt ' + name);
    loadSettings('users', 'user.' + name, function (js) {
        console.log('doLogin result: ' + JSON.stringify(js));
        if (!js['user.' + name]) {
            errorDialog('No such user: ' + name);
            cb();
            return;
        }
        var userData = JSON.parse(js['user.' + name]);
        if (hash != userData['password']) {
            console.log('Your hash was ' + hash);
            console.log('Desired hash was ' + userData['password']);
            console.log('Username was ' + name);
            errorDialog('Bad password. Check log for proper hash and edit user file.');
            cb();
            return;
        }
        setCookie('login', name + ':' + hash, 1000000);
        if($('#login').css('display') == 'block') {
            $('#login').slideUp(function() {
                $('div.tdropdown.username').slideDown();
            });
        } else {
            $('div.tdropdown.username').show();
        }
        $('span#username').text(name);
        theUser = js;
        theUserName = name;
        $('#save_dashboard').removeClass('disabled');
        theDashboards.reload();
        cb();
    },
    function (js) {
        choiceDialog("Could not load user info -- create new user " + name + "?",
            {'create': 'Yes', 'no': 'No'},
            function(opt) {
                if (opt == 'create') {
                    createUser(name, password, hash, function() {
                        doLogin(name, password, pwHashed, cb);
                    });
                }
                else {
                    cb();
                }
            });
    });
}

function login() {
    var name = $('#loginname').val();
    var password = $('#loginpassword').val();
    if (!name || !password || name.match(/[^a-zA-Z_0-9]/)) {
        errorDialog("You must enter a name and password to login.");
        return;
    }
    doLogin(name, password, null, done);
}

function logout() {
    setCookie('login', '', -1);
    $('div.tdropdown.username').fadeOut(function() {
        window.location.href = window.location.href.split('#')[0];
    });
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
    var end = begin();
    var date = new Date();
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
    3: 'customBars'
};

var bars_fmt_to_ix = {
    'noBars': 1,
    'errorBars': 2,
    'customBars': 3
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


var on_ready = guard(function _on_ready() {
    theTabs = new TabCollection('lefttab');
    theCounters = new CounterHierarchy('counters', theTabs.widget);
    theDashboards = new DashboardList('dashboards', theTabs.widget);
    theSplitter = new HSplitter('hsplit');
    theGrid = new GraphGrid('grid');
    theTimeSlider = new TimeSlider('time_slider');

    $('#hsplit').attr('unselectable', 'on');
    $('.tbutton').attr('unselectable', 'on');
    $('.tmenu div').attr('unselectable', 'on');
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
    $('.tmenu, .tdropdown').each(function(ix, item) {
        var $i = $(item);
        $i.click(function(ev) {
            ev.stopPropagation();
            $i.toggleClass('open');
        });
        $('body').mouseup(function() {
            $i.removeClass('open');
        });
    });
    $('.tmenuitem').each(function(ix, item) {
        (function() {
            var $i = $(item);
            $i.attr('title', $.trim($i.text()));
            $i.click(guard(function(ev) {
                ev.stopPropagation();
                var txt = $i.attr('action');
                eval(txt);
            }))
        })();
    });
    $('#loginname').keydown(function(ev) {
        if (ev.which == 13) {
            ev.stopPropagation();
            $('#loginpassword').focus();
        }
    });
    $('#loginpassword').keydown(function(ev) {
        if (ev.which == 13) {
            ev.stopPropagation();
            login();
        }
    });
    $('#loginname, #loginpassword').focus(function(ev) {
        $(this).unbind('focus');
        $(this).val('');
    });

    var $clear = $('#counter_filter_clear');
    var $label = $('#counter_filter_label');
    var $input = $('#counter_filter_text');

    if($input.val()) {
        $label.hide();
    }
    $label.live('click', function(ev) {
        $input.focus();
    });
    $input.live('focus', function(ev) {
        $label.hide();
    });
    $input.live('blur', function(ev) {
        if(!$input.val()) {
            $label.show();
        }
    });
    $clear.live('click', function(ev) {
        $input.val("");
        $label.show();
        theCounters.update();
    });

    $('#counter_filter_text').live('keydown', function(ev) {
        if (ev.which == 13) {
            ev.stopPropagation();
            theCounters.update();
            if(!$input.val()) {
                $label.show();
            }
        }
    });
    $(window).resize(documentResize);
    theLoader = new Loader();
    maybeParseCookie(document.cookie, gotohash);
    window.onhashchange = gotohash;
})
