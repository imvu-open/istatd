#!/usr/bin/env python3
import sys
from collections import Counter

if len(sys.argv) < 3:
    print('usage: compare_file.py <file1> <file2>')
    sys.exit(1)

def read_file(filename):
    with open(filename, 'rt') as file:
        return file.readlines()

def sort_line(line):
    return ''.join(sorted(line))

data1 = list(read_file(sys.argv[1]))
data2 = list(map(sort_line, read_file(sys.argv[2])))
data1.sort()
data2.sort()

def compare_file(first, second):
    if len(first) != len(second):
        print('first has {0} lines, second has {1}'.format(len(first), len(second)))
        return False

    for line in first:
        if sort_line(line) not in second:
            print('first differs:')
            print('{0}'.format(line))
            return False

    return True

if not compare_file(data1, data2):
    sys.exit(1)
