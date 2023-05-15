#!/usr/bin/env python3
import json
import pprint
import sys

if len(sys.argv) < 3:
    print('usage: compare_json.py <file1> <file2>')
    sys.exit(1)

def read_file(filename):
    with open(filename, 'rt') as file:
        return json.load(file)

data1 = read_file(sys.argv[1])
data2 = read_file(sys.argv[2])

def compare_json(first, second):
    if len(first.keys()) != len(second.keys()):
        print('first has {0} keys, second has {1}'.format(len(first.keys()), len(second.keys())))
        return False

    if first['type_mapping'] != second['type_mapping']:
        print('type_mapping differs')
        print('first: {0}'.format(pprint.pformat(first['type_mapping'], compact=True)))
        print('second: {0}'.format(pprint.pformat(second['type_mapping'], compact=True)))
        return False

    if first['pattern'] != second['pattern']:
        print('pattern differs')
        print('first: {0}'.format(pprint.pformat(first['pattern'], compact=True)))
        print('second: {0}'.format(pprint.pformat(second['pattern'], compact=True)))
        return False

    if len(first['matching_names']) != len(second['matching_names']):
        print('first has {0} matching_names, second has {1}'.format(len(first['matching_names']), len(second['matching_names'])))
        return False

    firstNames = sorted(first['matching_names'], key=lambda item: item['name'])
    secondNames = sorted(second['matching_names'], key=lambda item: item['name'])
    if firstNames != secondNames:
        print('matching_names differ')
        print('first: {0}'.format(pprint.pformat(firstNames, compact=True)))
        print('second: {0}'.format(pprint.pformat(secondNames, compact=True)))
        return False
    
    return True

if not compare_json(data1, data2):
    sys.exit(1)
