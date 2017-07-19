#!/bin/bash

### BEGIN INIT INFO
# Provides:             istatd-server
# Required-Start:  
# Required-Stop:   
# Default-Start:        2 3 4 5
# Default-Stop:         0 1 6
# Short-Description:    Start collecting statistics
# Description:          Istatd collects statistics for later graphing and analysis
### END INIT INFO

if [ `whoami` != 'root' ]; then
    echo "You must be root!"
    exit 1
fi

DESC="IMVU Statistics Server Daemon"
NAME=istatd-server
PIDFILE=/var/run/$NAME.pid
HOSTNAME=`hostname -s`
LOCALSTATS="^host.$HOSTNAME"
DAEMON="/usr/bin/istatd-server"
CONFIG=/etc/istatd-server.cfg
LOGFILE=/var/log/istatd-server.log

#source defaults file
[ -f /etc/default/istatd-server ] && . /etc/default/istatd-server

# first, cd to a "safe" place
cd /var/tmp

is_istatd_running() {
    #in shell land, 0 is true!
    if [ -s $PIDFILE ] && ps `cat $PIDFILE` | grep -q $DAEMON > /dev/null ; then
        return 0
    elif ps -FC $NAME | grep -q $DAEMON > /dev/null ; then
        return 0
    else
        return 1
    fi
}

start_istatd() {
    if [ "$ENABLE_ISTATD" -eq 1 ]; then  
        echo -n "Starting $DESC: "
        "$DAEMON" \
            --daemonize \
            --pid-file "$PIDFILE" \
            --config "$CONFIG" \
            --log-file "$LOGFILE" \
            $EXTRA_ARGS
        echo -n "$NAME."
        echo
    else
        echo "ISTATD Server is not enabled. Modify /etc/default/istatd-server to enable."
    fi
}

stop_istatd() {
  echo -n "Stopping $DESC: "
  I=0
  while [ $I -lt 10 ] && is_istatd_running ; do
    let I=$I+1

    if [ -s $PIDFILE ] && ps `cat $PIDFILE` | grep -q $DAEMON > /dev/null ; then
        kill -TERM `cat $PIDFILE`
    elif ps -FC $NAME | grep -q $DAEMON > /dev/null ; then
        kill -TERM `ps -FC $NAME | grep $DAEMON | awk '{print $2}'`
    fi

    if ! is_istatd_running ; then
        break
    fi
    sleep 1
  done

  if is_istatd_running ; then
    echo "failed"
  else
    echo "stopped"
  fi
}

status_istatd() {
  if is_istatd_running ; then
     echo $NAME is running
  else
     echo $NAME is stopped
  fi
}
    
case "$1" in
    start)
        start_istatd
        ;;
    stop)
        stop_istatd
        ;;
    status)
        status_istatd
        ;;
    restart)
        stop_istatd
        start_istatd
        ;;
    *)
        echo "Usage: /etc/init.d/$NAME {start|stop|restart|status}" >&2
        exit 1
        ;;
esac
