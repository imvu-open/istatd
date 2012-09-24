#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"

test_name bad_listen_address_aborts_on_startup
if ! /bin/echo main shell $(start_server single --listen-address badip) | grep -q "Unable to launch" ; then
   failure "Expected launch of server with bad listen address to fail"
fi

# this section blindly stolen from the json interface testing code
create_stat_files restrictedlisten foo.bar 1000 1100
create_stat_files restrictedlisten foo.bar 1200 1300 # even gap
create_stat_files restrictedlisten foo.bar 1390 1500 # odd gap
create_stat_files restrictedlisten foo.bar 1610 2570 # 16 minute run

test_name restricted_listen_address_on_correct_ip
start_server restrictedlisten --fake-time 2600
http_get_counter 127.0.0.1:18013 1611 2509 45 foo.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name restricted_listen_address_on_incorrect_ip
http_get_counter 127.0.0.2:18013 1611 2509 45 foo.bar > $TEST_OUT
assert_expected $TEST_OUT

cleanup_test

