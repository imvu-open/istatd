#Gather system metrics via collectd

You can use [collectd](http://collectd.org/) to gather various statistics
for your system and write those statistics to istatd using the istatd
plugin located in contrib/collectd/plugin directory.  To build the
plugins in this directory, you will need to download collectd to get
source required to compile this plugin.

## istatd write plugin for collectd

To enable the istatd write plugin, run make install and add the following
to your /etc/collectd/collectd.conf

```
LoadPlugin istatd

<Plugin istatd>
    Port "8111"
</Plugin>
```

Note here that Port 8111 is the stat port of the istatd agent running
on the host where collectd is gathering metrics.

## icpu read plugin for collectd
You can use this plugin to store cpu idle, user, etc statistics as
percentages.  Caution, the istatd plugin (see above) maps icpu plugin
to the cpu namespace, so you can not use the collectd cpu plugin with
the icpu pluging.

To enable the icpu plugin, run make install add the following to your
/etc/collectd/collectd.conf

```
LoadPlugin icpu
```
