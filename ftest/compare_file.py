#!/usr/bin/env python3
import sys

if len(sys.argv) < 3:
    print('usage: compare_file.py <file1> <file2>')
    sys.exit(1)

def read_file(filename):
    with open(filename, 'rt') as file:
        return file.readlines()

data1 = read_file(sys.argv[1])
data2 = set(read_file(sys.argv[2]))

def compare_file(first, second):
    if len(first) != len(second):
        print('first has {0} lines, second has {1}'.format(len(first), len(second)))
        return False

    for line in data1:
        if line not in data2:
            print('first differs:')
            print('{0}'.format(line))
            return False

    return True 

if not compare_file(data1, data2):
    sys.exit(1)
