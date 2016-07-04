#!/usr/bin/env python3
#
# Copyright (c) 2016 UAVCAN Team
#
# This script can be used to quickly obtain data type signature value for any UAVCAN data type.
# Execute with --help to get usage info.
#

''''which python3 >/dev/null 2>&1 && exec python3 "$0" "$@" # '''
''''which python  >/dev/null 2>&1 && exec python  "$0" "$@" # '''
''''which python2 >/dev/null 2>&1 && exec python2 "$0" "$@" # '''
''''exec echo "Python not found" >&2 # '''

# We can't import from __future__ here because of the wickedness above.
import sys
try:
    import uavcan
except ImportError:
    sys.stderr.write('PyUAVCAN is not installed. Please install from PIP: sudo pip3 install uavcan\n')
    exit(1)

# TODO: Add support for vendor-specific data types!
if '--help' in sys.argv:
    print('Usage: %s' % sys.argv[0])
    exit(0)

longest_name = max(map(len, uavcan.TYPENAMES.keys()))

header = 'Full Data Type Name'.ljust(longest_name) + ' | DDTID |   Type Signature   |  Max Bit Len  '
print(header)
print('-' * len(header))

for typename, typedef in sorted(uavcan.TYPENAMES.items()):
    ddtid = typedef.default_dtid if typedef.default_dtid is not None else 'N/A'
    s = '%-*s   % 5s   0x%016x' % (longest_name, typename, ddtid, typedef.get_data_type_signature())
    try:
        s += '   % 5d' % typedef.get_max_bitlen()
    except Exception:
        s += '   % 5d / %-5d' % (typedef.get_max_bitlen_request(), typedef.get_max_bitlen_response())
    print(s)
