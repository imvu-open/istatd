#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"

create_stat_files single foo.bar 1000 1100
create_stat_files single foo.bar 1200 1300 # even gap
create_stat_files single foo.bar 1390 1500 # odd gap
create_stat_files single foo.bar 1610 2570 # 16 minute run

create_stat_files single ice.cream.bar 1010 1090

start_server single --fake-time 2600

send_stat single *maybe^counter 1
send_stat single maybe^gauge 1

test_name GET_returns_15_minutes_only_when_end_is_specified
http_get_counter localhost:18011 null 2510 null foo.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name GET_returns_15_minutes_only_when_start_is_specified
http_get_counter localhost:18011 1610 null null foo.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name GET_returns_last_15_minutes_of_data_when_start_and_end_is_not_specified
http_get_counter localhost:18011 null null null foo.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name GET_returns_error_when_end_is_less_than_start
http_get_counter localhost:18011 1390 1200 null foo.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name GET_returns_error_when_end_is_negative
http_get_counter localhost:18011 1390 -1 null foo.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name GET_returns_error_when_start_is_negative
http_get_counter localhost:18011 -1 1200 null foo.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name GET_returns_1_bucket_when_diff_of_end_and_start_is_less_than_interval
http_get_counter localhost:18011 1391 1398 null foo.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name GET_returns_empty_data_when_start_and_end_are_before_than_oldest_data_in_file
http_get_counter localhost:18011 500 600 null foo.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name GET_returns_empty_data_when_start_and_end_are_more_than_an_hour_later_than_the_last_data_in_file
http_get_counter localhost:18011 10000 12000 null foo.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name GET_returns_normalized_start_and_stop_when_data_missing_from_both_ends
http_get_counter localhost:18011 955 1345 null foo.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name GET_returns_finest_resolution_when_max_samples_is_greater_than_number_of_samples_in_the_range
http_get_counter localhost:18011 1000 1100 100 foo.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name GET_increases_size_of_bucket_when_max_samples_is_limited_by_the_caller
http_get_counter localhost:18011 1000 1100 5 foo.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name GET_buckets_on_interval_boundry_when_limiting_number_of_samples_and_start_time_not_a_multiple_of_interval
http_get_counter localhost:18011 1611 2509 45 foo.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name GET_does_not_return_buckets_where_there_are_gaps
http_get_counter localhost:18011 1000 1600 600 foo.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name GET_does_not_return_buckets_where_there_are_gaps_and_when_samples_are_limited_by_user
http_get_counter localhost:18011 1000 1600 30 foo.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name POST_returns_a_value_when_one_counter_specified
http_post_for_counters localhost:18011 1000 1600 600 foo.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name POST_returns_a_value_when_two_counters_are_specified
http_post_for_counters localhost:18011 1000 1600 600 foo.bar ice.cream.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name POST_returns_normalized_start_and_stop_times
http_post_for_counters localhost:18011 1001 1598 600 foo.bar ice.cream.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name POST_returns_error_when_start_negative
http_post_for_counters localhost:18011 -1 1598 600 foo.bar ice.cream.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name POST_returns_error_when_end_negative
http_post_for_counters localhost:18011 1001 -1 600 foo.bar ice.cream.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name POST_returns_error_when_end_before_start
http_post_for_counters localhost:18011 1598 1001 600 foo.bar ice.cream.bar > $TEST_OUT
assert_expected $TEST_OUT

test_name POST_requires_keys_array
http_post_json "http://localhost:18011/*" '{"start":1001,"stop":1598,"maxSamples":2}' > $TEST_OUT
assert_expected $TEST_OUT

test_name POST_works_with_urlencoded_query_string
http_post_json "http://localhost:18011/%2A" '{"start":1001,"stop":1598,"maxSamples":2,"keys":["foo.bar"]}' > $TEST_OUT
assert_expected $TEST_OUT

test_name POST_works_with_trailing_key
http_post_json "http://localhost:18011/*" '{"start":1001,"stop":1598,"maxSamples":2,"keys":["foo.bar"],"trailing":0}' > $TEST_OUT
assert_expected $TEST_OUT

test_name POST_works_with_compact_key
http_post_json "http://localhost:18011/*" '{"start":1001,"stop":1598,"maxSamples":2,"keys":["foo.bar"],"compact":true}' > $TEST_OUT
assert_expected $TEST_OUT

test_name GET_counter_list_returns_counter_type
curl -s "http://localhost:18011/?q=*" > $TEST_OUT
assert_expected $TEST_OUT

kill_server single
start_server single --fake-time 2600

test_name GET_counter_list_returns_counter_type
curl -s "http://localhost:18011/?q=*" > $TEST_OUT
assert_expected_json $TEST_OUT


cleanup_test
