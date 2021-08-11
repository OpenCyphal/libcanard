#!/usr/bin/env python3
#
# Copyright (c) 2016 UAVCAN Team
#
# This script can be used to quickly obtain data type signature value for any UAVCAN data type.
# Execute with --help to get usage info.
#

import sys  # noqa: E402
import getopt  # noqa: E402

try:
    import pyuavcan_v0 as uavcan
except ImportError:
    sys.stderr.write('PyUAVCAN v0 is not installed. Please install from PIP: pip install pyuavcan_v0\n')
    exit(1)


def printUsage():
    print("""show_data_type_info:
    [-h, --help]: show this help
    [-c, --custom] [path/to/custom/types]: path to your custom types '00.mymsgtype.uavcan'
    """)


if __name__ == "__main__":
    # decode options given to the script
    try:
        opts, args = getopt.getopt(sys.argv[1:], 'hc:', ['help', 'custom='])
    except getopt.GetoptError:
        printUsage()
        sys.exit()

    # include/execute options given to the script
    for opt, arg in opts:
        if opt in ('-h', '--help'):
            printUsage()
            sys.exit()
        elif opt in ('-c', '--custom'):
            uavcan.load_dsdl(arg)

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
