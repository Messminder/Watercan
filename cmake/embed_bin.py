#!/usr/bin/env python3
import sys
import os

if len(sys.argv) < 4:
    print("Usage: embed_bin.py <input> <varname> <output>", file=sys.stderr)
    sys.exit(2)

in_path = sys.argv[1]
var_name = sys.argv[2]
out_path = sys.argv[3]

with open(in_path, 'rb') as f:
    data = f.read()

with open(out_path, 'w') as f:
    f.write('#include <cstddef>\n')
    # Use extern to ensure external linkage (C++ const has internal linkage by default)
    f.write('extern const unsigned char %s[] = {\n' % var_name)
    for i, b in enumerate(data):
        if i % 12 == 0:
            f.write('    ')
        f.write(str(b))
        if i != len(data) - 1:
            f.write(',')
        if (i % 12) != 11:
            f.write(' ')
        if (i % 12) == 11:
            f.write('\n')
    f.write('\n};\n')
    size_var = var_name + '_len'
    f.write('extern const std::size_t %s = %d;\n' % (size_var, len(data)))
