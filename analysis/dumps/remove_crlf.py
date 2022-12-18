'''
Converts utf-16 encoded file removing carriage returns from the file.
'''

import sys

file_contents = ''

with open(sys.argv[1], 'r', encoding='utf-8') as f:
    for line in f:
        if line[-2:] == '\r\n':
            line = line[:-2] + '\n'
        file_contents += line

with open(sys.argv[1] + '.str', 'w') as f:
    f.write(file_contents)
