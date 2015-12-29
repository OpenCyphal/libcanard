#!/usr/bin/env python
"""
Solves the DSDL type hierarchy.
"""

import argparse
import uavcan

from jinja2 import Environment, FileSystemLoader
JINJA_ENV = Environment(loader=FileSystemLoader(['.']))

def depends_on(dependencies, package):
    result = set()
    for key, packages in dependencies.items():
        if package in packages:
            result.add(key)

    return result

def topological_sort(dependencies):
    # Set of all node with no dependencies
    S = {k for k, v in dependencies.items() if not v}

    while S:
        current = S.pop()
        yield current

        for m in depends_on(dependencies, current):
            dependencies[m].remove(current)

            if not dependencies[m]:
                S.add(m)

def solve_types_dependency(types):
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

def type_uavcan_to_c(uavcan_type):

    if uavcan_type.category is uavcan_type.CATEGORY_COMPOUND:
        return "struct {}".format(uavcan_type.full_name.replace('.', '_').lower())

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


def parse_args():
    """
    Parse commandline args.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('-d', '--dsdl', action='append',
                        dest='src_directories',
                        help='Folder containing DSDL files.')

    return parser.parse_args()

def main():
    args = parse_args()

    types = uavcan.dsdl.parse_namespaces(args.src_directories)

    template = JINJA_ENV.get_template('header.jinja')

    types = solve_types_dependency(types)

    print(template.render(types=types, uavcan_type_to_c=type_uavcan_to_c))

if __name__=='__main__':
    main()
