#!/usr/bin/env python
"""
Generates serialization functions for libcanard from UAVCAN DSDL files.
"""
import argparse
import uavcan

INDENT = " " * 4

def parse_args():
    """
    Parse commandline args.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('-d', '--dsdl', action='append',
                        dest='src_directories',
                        help='Folder containing DSDL files.')

    return parser.parse_args()

def type_uavcan_to_c(uavcan_type):
    if uavcan_type.category is not uavcan.dsdl.Type.CATEGORY_PRIMITIVE:
        raise ValueError("Cannot convert non primitive type to C")

    if uavcan_type.kind is uavcan_type.KIND_FLOAT:
        return "float"
    elif uavcan_type.kind is uavcan_type.KIND_BOOLEAN:
        return "bool"
    elif uavcan_type.kind is uavcan_type.KIND_SIGNED_INT:
        return "int32_t"
    elif uavcan_type.kind is uavcan_type.KIND_UNSIGNED_INT:
        return "uint32_t"

    raise ValueError("Unhandled type kind {}".format(uavcan_type.kind))


def render_field(field, indent=0):
    result = ""
    if field.type.category is uavcan.dsdl.Type.CATEGORY_COMPOUND:
        result += INDENT * indent + "struct (\n"

        for f in field.type.fields:
            result += render_field(f, indent+1)

        result += INDENT * indent + ") {}\n".format(field.name)

    elif field.type.category is uavcan.dsdl.Type.CATEGORY_PRIMITIVE:
        result += INDENT * indent + type_uavcan_to_c(field.type) + " " + field.name + "\n"

    return result



def render_type(uavcan_type):
    """
    Renders the given type as the appropriate C type.
    """

    result = ""

    c_name = uavcan_type.full_name.replace('.', '_')

    if uavcan_type.category is uavcan.dsdl.Type.CATEGORY_COMPOUND:
        result += "struct {} (\n".format(c_name)

        for f in uavcan_type.fields:
            result += render_field(f, 1)

        result += ")\n"

    return result



def main():
    args = parse_args()

    types = uavcan.dsdl.parse_namespaces(args.src_directories)

    for t in types:
        print(render_type(t))


if __name__=='__main__':
    main()

