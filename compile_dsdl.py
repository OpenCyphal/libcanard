#!/usr/bin/env python
"""
Solves the DSDL type hierarchy.
"""

import argparse
import uavcan

from jinja2 import Environment, FileSystemLoader
JINJA_ENV = Environment(loader=FileSystemLoader(['.']))


def topological_sort(dependencies):
    """
    Performs a topological sort of the given dependency graph.

    The dependency graph is given as a dictionary mapping each key to a list of
    its dependencies.

    Raises ValueError on a cyclic dependency graph.
    """
    def depends_on(dependencies, package):
        result = set()
        for key, packages in dependencies.items():
            if package in packages:
                result.add(key)

        return result

    # Set of all node with no dependencies
    S = {k for k, v in dependencies.items() if not v}

    while S:
        current = S.pop()
        yield current

        for m in depends_on(dependencies, current):
            dependencies[m].remove(current)

            if not dependencies[m]:
                S.add(m)

    # If there are still nodes with dependencies at this stage it means the
    # graph is cyclic.
    if any(dep for dep in dependencies.values()):
        raise ValueError("Cyclic dependency graph.")

def solve_types_dependency(types):
    """
    Given a list of UAVCAN types return them in an order where every type comes
    before types using it.
    """
    name_to_types = dict()
    for t in types:
        name_to_types[t.full_name] = t

    dependencies = dict()
    for t in types:
        dependencies[t.full_name] = set()

        for f in t.fields:
            if f.type.category == f.type.CATEGORY_COMPOUND:
                dependencies[t.full_name].add(f.type.full_name)

    for t in topological_sort(dependencies):
        yield name_to_types[t]

    return []

def normalized_name(uavcan_type):
    """
    Returns the C name for the given uavcan name.
    """
    return uavcan_type.full_name.replace('.', '_').lower()

def type_uavcan_to_c(uavcan_type):

    if uavcan_type.category is uavcan_type.CATEGORY_COMPOUND:
        return "struct {}".format(normalized_name(uavcan_type))

    if uavcan_type.category is not uavcan.dsdl.Type.CATEGORY_PRIMITIVE:
        raise ValueError("Cannot convert non primitive type to C")

    if uavcan_type.kind is uavcan_type.KIND_FLOAT:
        if uavcan_type.bitlen <= 32:
            return "float"
        else:
            return "double"
    elif uavcan_type.kind is uavcan_type.KIND_BOOLEAN:
        return "bool"
    elif uavcan_type.kind is uavcan_type.KIND_SIGNED_INT:
        if uavcan_type.bitlen <= 8:
            return "int8_t"
        elif uavcan_type.bitlen <= 16:
            return "int16_t"
        elif uavcan_type.bitlen <= 32:
            return "int32_t"
        else:
            return "int64_t"
    elif uavcan_type.kind is uavcan_type.KIND_UNSIGNED_INT:
        if uavcan_type.bitlen <= 8:
            return "uint8_t"
        elif uavcan_type.bitlen <= 16:
            return "uint16_t"
        elif uavcan_type.bitlen <= 32:
            return "uint32_t"
        else:
            return "uint64_t"

    raise ValueError("Unhandled type kind {}".format(uavcan_type.kind))


def parse_args():
    """
    Parse commandline args.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('-d', '--dsdl', action='append',
                        dest='src_directories',
                        required=True,
                        help='Folder containing DSDL files.')

    parser.add_argument('--header', type=argparse.FileType('w'),
                        help='Generate the header file for libcanard.')

    return parser.parse_args()

def main():
    args = parse_args()

    types = uavcan.dsdl.parse_namespaces(args.src_directories)

    template = JINJA_ENV.get_template('header.jinja')

    types = list(solve_types_dependency(types))

    if args.header:
        args.header.write(template.render(types=types,
                              uavcan_type_to_c=type_uavcan_to_c,
                              normalized_name=normalized_name,
                              ))

if __name__=='__main__':
    main()
