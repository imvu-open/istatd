

How to test the individual pieces
=================================

Generate a file with a bunch of data:

    bin/random_numbers -t 100000 1000 | bin/istatd_nums2file /tmp/file.cf -t 10

Dump the file to CSV format (calculating sdev etc):

    bin/istat_filedump /tmp/file.cf

Get information about a counter file (checking header fields):

    bin/istat_fileinfo /tmp/file.cf
