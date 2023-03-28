'''
Generate .dump file from the pdb file specified in the json config file. Uses
cvdump.exe to generate the dump. The resulting dump file will be written to the
dumpFile specified in the config file.
'''

import os
import pathlib
import sys

fpath = pathlib.Path(__file__).parent.absolute()

sys.path.append(os.path.join(fpath, '..'))

from parseconfig import config

cvdump_exe = os.path.join(fpath, 'cvdump.exe')

pdb_to_dump = config['pdbFile']
dumpfile = config['dumpFile']

import subprocess
with open(dumpfile, "wb") as outfile:
    process = subprocess.check_call([cvdump_exe, pdb_to_dump], shell=True,
                          stdout=outfile, stderr=subprocess.STDOUT)
