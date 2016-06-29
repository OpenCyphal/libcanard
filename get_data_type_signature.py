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
    print('Usage: %s [data type name]' % sys.argv[0])
    exit(0)

if len(sys.argv) < 2:
    longest_name = max(map(len, uavcan.TYPENAMES.keys()))
    for typename, typedef in uavcan.TYPENAMES.items():
        print('%-*s 0x%016x' % (longest_name, typename, typedef.get_data_type_signature()))
else:
    typename = sys.argv[1]
    try:
        signature = uavcan.TYPENAMES[typename].get_data_type_signature()
    except KeyError:
        sys.stderr.write('Data type not found: %r\n' % typename)
        exit(1)
    else:
        print('0x%016x' % signature)
