#!/usr/bin/perl
use strict;

use IO::Socket::INET;
use Time::HiRes;
$| = 1;

#  ps eo pid,rss,size,vsize 5992
my $g_verbose = 0;
my $g_rss = 0;
my $g_vsize = 0;
my $g_connections = 0;
my $g_proc_read = 0.0;
my $g_proc_wait = 0.0;
my $g_connect_wait = 0.0;
my $g_start_time = time();

sub get_proc_stat {
    my $pid = shift;
    my $t0 = [ Time::HiRes::gettimeofday() ];
    if (open(PROC, "/proc/$pid/stat")) {
        my $line = <PROC>;
        chomp($line);
        my @line = split(/\s+/, $line);
        my ($pid, $comm, $state, $ppid, $pgrp,
            $session, $tty_nr, $tpgid, $flags,
            $minflt, $cminflt, $majflt, $cmajflt, 
            $utime, $stime, $cutime, $cstime, 
            $priority, $nice, $num_threads, 
            $itrealvalue, $starttime, $vsize,
            $rss, $rsslim, $startcode, $endcode,
            $startstack, $kstkesp, $kstkeip, $signal,
            $blocked, $sigignore, $sigcatch, $wchan, $nswap,
            $cnswap, $exit_signal, $processor, $rt_priority,
            $policy, $delayacct_blkio_ticks, $guest_time,
            $cguest_time) = @line;
        close PROC;
        $g_rss = $rss;
        $g_vsize = $vsize;
        
        my $te = Time::HiRes::tv_interval($t0);
        $te *= 1000;

        $g_proc_read = $te;
        $g_proc_wait += $te;
        return \@line;
    } else {
        warn("Unable to read proc info for pid '$pid'\n");
    }
    return undef;
}

sub push_connection {
    my $t0 = [ Time::HiRes::gettimeofday() ];
    my $socket = new IO::Socket::INET (
        PeerHost => '127.0.0.1',
        PeerPort => '8111',
        Proto => 'tcp',
        ) or die "ERROR in Socket Creation : $!.\n";
    my $te = Time::HiRes::tv_interval($t0);
    $te *= 1000;
    $g_connect_wait += $te;
    print $socket "*memleak.connected 1\n";
    print $socket "memleak.rss $g_rss\n";
    print $socket "memleak.vsize $g_vsize\n";
    print $socket "memleak.connections $g_connections\n";
    print $socket "memleak.connect_millis $te\n";
    print $socket "memleak.connect_wait_millis $g_connect_wait\n";
    $socket->close();
}

sub push_mem_info {
    my $socket = new IO::Socket::INET (
        PeerHost => '127.0.0.1',
        PeerPort => '8111',
        Proto => 'tcp',
        ) or die "ERROR in Socket Creation : $!\n.";
    if ($g_rss eq '') {
        warn("g_rss is empty");
    }
    my $elapsed = time() - $g_start_time;
    print $socket "memleak.rss $g_rss\n";
    print $socket "memleak.vsize $g_vsize\n";
    print $socket "memleak.procread_millis $g_proc_read\n";
    print $socket "memleak.procwait_millis $g_proc_wait\n";
    print $socket "memleak.connections $g_connections\n";
    print $socket "memleak.elapsed_secs $elapsed\n";
    if ($g_verbose) {
        print "memleak.rss $g_rss\n";
        print "memleak.vsize $g_vsize\n";
        print "memleak.connections $g_connections\n";
    }
    $socket->close();
}

while ($g_connections < 30000) {
    if (($g_connections % 100) == 0) {
        print ".";
        get_proc_stat($ARGV[0]);
        push_mem_info();
    }
    push_connection();
    $g_connections += 1;
}
