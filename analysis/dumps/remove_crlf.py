'''
Converts utf-16 encoded file removing carriage returns from the file.
'''

import os
import sys
print(sys.argv[1])

file_contents = ''

with open(sys.argv[1], 'r', encoding='utf-16') as f:
    for line in f:
        if line[-2:] == '\r\n':
            line = line[:-2] + '\n'
        file_contents += line

with open(sys.argv[1] + '.str', 'w') as f:
    f.write(file_contents)
