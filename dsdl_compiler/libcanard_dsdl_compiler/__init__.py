#
# UAVCAN DSDL compiler for libcanard
#
# This code is written by Pavel Kirienko for libuavcan DSDL generator
# copied and modified for the libcanard use
#
# Copyright (C) 2014 Pavel Kirienko <pavel.kirienko@gmail.com>
# Copyright (C) 2018 Intel Corporation
#

'''
This module implements the core functionality of the UAVCAN DSDL compiler for libcanard.
Supported Python versions: 3.2+, 2.7.
It accepts a list of root namespaces and produces the set of C header files and souce files for libcanard.
It is based on the DSDL parsing package from pyuavcan.
'''

from __future__ import division, absolute_import, print_function, unicode_literals
import sys, os, logging, errno, re
from .pyratemp import Template
from uavcan import dsdl

# Python 2.7 compatibility
try:
    str = unicode
except NameError:
    pass

OUTPUT_HEADER_FILE_EXTENSION = 'h'
OUTPUT_CODE_FILE_EXTENSION = 'c'
HEADER_TEMPLATE_FILENAME = os.path.join(os.path.dirname(__file__), 'data_type_template.tmpl')
CODE_TEMPLATE_FILENAME = os.path.join(os.path.dirname(__file__), 'code_type_template.tmpl')

__all__ = ['run', 'logger', 'DsdlCompilerException']

class DsdlCompilerException(Exception):
    pass

logger = logging.getLogger(__name__)

def run(source_dirs, include_dirs, output_dir, header_only):
    '''
    This function takes a list of root namespace directories (containing DSDL definition files to parse), a
    possibly empty list of search directories (containing DSDL definition files that can be referenced from the types
    that are going to be parsed), and the output directory path (possibly nonexistent) where the generated C++
    header files will be stored.

    Note that this module features lazy write, i.e. if an output file does already exist and its content is not going
    to change, it will not be overwritten. This feature allows to avoid unnecessary recompilation of dependent object
    files.

    Args:
        source_dirs    List of root namespace directories to parse.
        include_dirs   List of root namespace directories with referenced types (possibly empty). This list is
                       automaitcally extended with source_dirs.
        output_dir     Output directory path. Will be created if doesn't exist.
        header_only    Weather to generated as header only library.
    '''
    assert isinstance(source_dirs, list)
    assert isinstance(include_dirs, list)
    output_dir = str(output_dir)

    types = run_parser(source_dirs, include_dirs + source_dirs)
    if not types:
        die('No type definitions were found')

    logger.info('%d types total', len(types))
    run_generator(types, output_dir, header_only)

# -----------------

def pretty_filename(filename):
    try:
        a = os.path.abspath(filename)
        r = os.path.relpath(filename)
        return a if '..' in r else r
    except ValueError:
        return filename

# get the CamelCase prefix from the current filename
def get_name_space_prefix(t):
    return t.full_name.replace('.', '_')

def type_output_filename(t, extension = OUTPUT_HEADER_FILE_EXTENSION):
    assert t.category == t.CATEGORY_COMPOUND
    folder_name = t.full_name.split('.')[-2]
    if extension == OUTPUT_CODE_FILE_EXTENSION:
        name_list = t.full_name.split('.')
        if len(folder_name):
            name_list[-1] = str(folder_name) + '_' + str(name_list[-1])
        return os.path.sep.join(name_list) + '.' + extension
    else:
        return t.full_name.replace('.', os.path.sep) + '.' + extension

def makedirs(path):
    try:
        try:
            os.makedirs(path, exist_ok=True)  # May throw "File exists" when executed as root, which is wrong
        except TypeError:
            os.makedirs(path)  # Python 2.7 compatibility
    except OSError as ex:
        if ex.errno != errno.EEXIST:  # http://stackoverflow.com/questions/12468022
            raise

def die(text):
    raise DsdlCompilerException(str(text))

def run_parser(source_dirs, search_dirs):
    try:
        types = dsdl.parse_namespaces(source_dirs, search_dirs)
    except dsdl.DsdlException as ex:
        logger.info('Parser failure', exc_info=True)
        die(ex)
    return types

def run_generator(types, dest_dir, header_only):
    try:
        header_template_expander = make_template_expander(HEADER_TEMPLATE_FILENAME)
        code_template_expander = make_template_expander(CODE_TEMPLATE_FILENAME)
        dest_dir = os.path.abspath(dest_dir)  # Removing '..'
        makedirs(dest_dir)
        for t in types:
            logger.info('Generating type %s', t.full_name)
            header_path_file_name = os.path.join(dest_dir, type_output_filename(t, OUTPUT_HEADER_FILE_EXTENSION))
            code_filename = os.path.join(dest_dir, type_output_filename(t, OUTPUT_CODE_FILE_EXTENSION))
            t.header_filename = type_output_filename(t, OUTPUT_HEADER_FILE_EXTENSION)
            t.name_space_prefix = get_name_space_prefix(t)
            t.header_only = header_only
            header_text = generate_one_type(header_template_expander, t)
            code_text = generate_one_type(code_template_expander, t)
            write_generated_data(header_path_file_name, header_text, header_only)
            if header_only:
                code_text = "\r\n" + code_text
                write_generated_data(header_path_file_name, code_text, header_only, True)
            else:
                write_generated_data(code_filename, code_text, header_only)
    except Exception as ex:
        logger.info('Generator failure', exc_info=True)
        die(ex)

def write_generated_data(filename, data, header_only, append_file=False):
    dirname = os.path.dirname(filename)
    makedirs(dirname)

    if append_file:
        with open(filename, 'a') as f:
            f.write(data)
    else:
        if os.path.exists(filename):
            os.remove(filename)
        with open(filename, 'w') as f:
            f.write(data)

def expand_to_next_full(size):
    if size <= 8:
        return 8
    elif size <= 16:
        return 16
    elif size <= 32:
        return 32
    elif size <=64:
        return 64

def get_max_size(bits, unsigned):
    if unsigned:
        return (2 ** bits) -1
    else:
        return (2 ** (bits-1)) -1

def strip_name(name):
    return name.split('.')[-1]

def type_to_c_type(t):
    if t.category == t.CATEGORY_PRIMITIVE:
        saturate = {
            t.CAST_MODE_SATURATED: True,
            t.CAST_MODE_TRUNCATED: False,
        }[t.cast_mode]
        cast_mode = {
            t.CAST_MODE_SATURATED: 'Saturate',
            t.CAST_MODE_TRUNCATED: 'Truncate',
        }[t.cast_mode]
        if t.kind == t.KIND_FLOAT:
            float_type = {
                16: 'float',
                32: 'float',
                64: 'double',
            }[t.bitlen]
            return {'cpp_type':'%s' % (float_type),
                    'post_cpp_type':'',
                    'cpp_type_comment':'float%d %s' % (t.bitlen, cast_mode, ),
                    'bitlen':t.bitlen,
                    'max_size':get_max_size(t.bitlen, False),
                    'signedness':'false',
                    'saturate':False} # do not saturate floats
        else:
            c_type = {
                t.KIND_BOOLEAN: 'bool',
                t.KIND_UNSIGNED_INT: 'uint',
                t.KIND_SIGNED_INT: 'int',
            }[t.kind]
            signedness = {
                t.KIND_BOOLEAN: 'false',
                t.KIND_UNSIGNED_INT: 'false',
                t.KIND_SIGNED_INT: 'true',
            }[t.kind]

            if t.kind == t.KIND_BOOLEAN:
                return {'cpp_type':'%s' % (c_type),
                    'post_cpp_type':'',
                    'cpp_type_comment':'bit len %d' % (t.bitlen, ),
                    'bitlen':t.bitlen,
                    'max_size':get_max_size(t.bitlen, True),
                    'signedness':signedness,
                    'saturate':saturate}
            else:
                if saturate:
                    # Do not staturate if struct field length is equal bitlen
                    if (expand_to_next_full(t.bitlen) == t.bitlen):
                        saturate = False
                return {'cpp_type':'%s%d_t' % (c_type, expand_to_next_full(t.bitlen)),
                    'post_cpp_type':'',
                    'cpp_type_comment':'bit len %d' % (t.bitlen, ),
                    'bitlen':t.bitlen,
                    'max_size':get_max_size(t.bitlen, t.kind == t.KIND_UNSIGNED_INT),
                    'signedness':signedness,
                    'saturate':saturate}

    elif t.category == t.CATEGORY_ARRAY:
        values = type_to_c_type(t.value_type)
        mode = {
            t.MODE_STATIC: 'Static Array',
            t.MODE_DYNAMIC: 'Dynamic Array',
        }[t.mode]
        return {'cpp_type':'%s' % (values['cpp_type'], ),
            'cpp_type_category': t.value_type.category,
            'post_cpp_type':'[%d]' % (t.max_size,),
            'cpp_type_comment':'%s %dbit[%d] max items' % (mode, values['bitlen'], t.max_size, ),
            'bitlen':values['bitlen'],
            'array_max_size_bit_len':t.max_size.bit_length(),
            'max_size':values['max_size'],
            'signedness':values['signedness'],
            'saturate':values['saturate'],
            'dynamic_array': t.mode == t.MODE_DYNAMIC,
            'max_array_elements': t.max_size,
            }
    elif t.category == t.CATEGORY_COMPOUND:
        return {
            'cpp_type':t.full_name.replace('.','_'),
            'post_cpp_type':'',
            'cpp_type_comment':'',
            'bitlen':t.get_max_bitlen(),
            'max_size':0,
            'signedness':'false',
            'saturate':False}
    elif t.category == t.CATEGORY_VOID:
        return {'cpp_type':'',
            'post_cpp_type':'',
            'cpp_type_comment':'void%d' % t.bitlen,
            'bitlen':t.bitlen,
            'max_size':0,
            'signedness':'false',
            'saturate':False}
    else:
        raise DsdlCompilerException('Unknown type category: %s' % t.category)

def generate_one_type(template_expander, t):
    t.name_space_type_name = get_name_space_prefix(t)
    t.cpp_full_type_name = '::' + t.full_name.replace('.', '::')
    t.include_guard = '__' + t.full_name.replace('.', '_').upper()
    t.macro_name = t.full_name.replace('.', '_').upper()

    # Dependencies (no duplicates)
    def fields_includes(fields):
        def detect_include(t):
            if t.category == t.CATEGORY_COMPOUND:
                return type_output_filename(t)
            if t.category == t.CATEGORY_ARRAY:
                return detect_include(t.value_type)
        return list(sorted(set(filter(None, [detect_include(x.type) for x in fields]))))

    if t.kind == t.KIND_MESSAGE:
        t.cpp_includes = fields_includes(t.fields)
    else:
        t.cpp_includes = fields_includes(t.request_fields + t.response_fields)

    t.cpp_namespace_components = t.full_name.split('.')[:-1]
    t.has_default_dtid = t.default_dtid is not None

    # Attribute types
    def inject_cpp_types(attributes):
        length = len(attributes)
        count = 0
        has_array = False
        for a in attributes:
            count = count + 1
            a.last_item = False
            if (count == length):
                a.last_item = True

            data = type_to_c_type(a.type)
            for key, value in data.items():
                setattr(a, key, value)

            if a.type.category == t.CATEGORY_ARRAY:
                a.array_size = a.type.max_size
                has_array = True

            a.type_category = a.type.category
            a.void = a.type.category == a.type.CATEGORY_VOID
            if a.void:
                assert not a.name
                a.name = ''
        return has_array

    def has_float16(attributes):
        has_float16 = False
        for a in attributes:
            if a.type.category == t.CATEGORY_PRIMITIVE and a.type.kind == a.type.KIND_FLOAT and a.bitlen == 16:
                has_float16 = True
        return has_float16

    if t.kind == t.KIND_MESSAGE:
        t.has_array = inject_cpp_types(t.fields)
        t.has_float16 = has_float16(t.fields)
        inject_cpp_types(t.constants)
        t.all_attributes = t.fields + t.constants
        t.union = t.union and len(t.fields)
        if t.union:
            t.union = len(t.fields).bit_length()
    else:
        t.request_has_array = inject_cpp_types(t.request_fields)
        t.request_has_float16 = has_float16(t.request_fields)
        inject_cpp_types(t.request_constants)
        t.response_has_array = inject_cpp_types(t.response_fields)
        t.response_has_float16 = has_float16(t.response_fields)
        inject_cpp_types(t.response_constants)
        t.all_attributes = t.request_fields + t.request_constants + t.response_fields + t.response_constants
        t.request_union = t.request_union and len(t.request_fields)
        t.response_union = t.response_union and len(t.response_fields)
        if t.request_union:
            t.request_union = len(t.request_fields).bit_length()
        if t.response_union:
            t.response_union = len(t.response_fields).bit_length()

    # Constant properties
    def inject_constant_info(constants):
        for c in constants:
            if c.type.kind == c.type.KIND_FLOAT:
                float(c.string_value)  # Making sure that this is a valid float literal
                c.cpp_value = c.string_value
            else:
                int(c.string_value)  # Making sure that this is a valid integer literal
                c.cpp_value = c.string_value
                if c.type.kind == c.type.KIND_UNSIGNED_INT:
                    c.cpp_value += 'U'

    if t.kind == t.KIND_MESSAGE:
        inject_constant_info(t.constants)
    else:
        inject_constant_info(t.request_constants)
        inject_constant_info(t.response_constants)

    # Data type kind
    t.cpp_kind = {
        t.KIND_MESSAGE: '::uavcan::DataTypeKindMessage',
        t.KIND_SERVICE: '::uavcan::DataTypeKindService',
    }[t.kind]

    # Generation
    text = template_expander(t=t)  # t for Type
    text = '\n'.join(x.rstrip() for x in text.splitlines())
    text = text.replace('\n\n\n\n\n', '\n\n').replace('\n\n\n\n', '\n\n').replace('\n\n\n', '\n\n')
    text = text.replace('{\n\n ', '{\n ')
    return text

def make_template_expander(filename):
    '''
    Templating is based on pyratemp (http://www.simple-is-better.org/template/pyratemp.html).
    The pyratemp's syntax is rather verbose and not so human friendly, so we define some
    custom extensions to make it easier to read and write.
    The resulting syntax somewhat resembles Mako (which was used earlier instead of pyratemp):
        Substitution:
            ${expression}
        Line joining through backslash (replaced with a single space):
            ${foo(bar(very_long_arument=42, \
                      second_line=72))}
        Blocks:
            % for a in range(10):
                % if a == 5:
                    ${foo()}
                % endif
            % endfor
    The extended syntax is converted into pyratemp's through regexp substitution.
    '''
    with open(filename) as f:
        template_text = f.read()

    # Backslash-newline elimination
    template_text = re.sub(r'\\\r{0,1}\n\ *', r' ', template_text)

    # Substitution syntax transformation: ${foo} ==> $!foo!$
    template_text = re.sub(r'([^\$]{0,1})\$\{([^\}]+)\}', r'\1$!\2!$', template_text)

    # Flow control expression transformation: % foo: ==> <!--(foo)-->
    template_text = re.sub(r'(?m)^(\ *)\%\ *(.+?):{0,1}$', r'\1<!--(\2)-->', template_text)

    # Block termination transformation: <!--(endfoo)--> ==> <!--(end)-->
    template_text = re.sub(r'\<\!--\(end[a-z]+\)--\>', r'<!--(end)-->', template_text)

    # Pyratemp workaround.
    # The problem is that if there's no empty line after a macro declaration, first line will be doubly indented.
    # Workaround:
    #  1. Remove trailing comments
    #  2. Add a newline after each macro declaration
    template_text = re.sub(r'\ *\#\!.*', '', template_text)
    template_text = re.sub(r'(\<\!--\(macro\ [a-zA-Z0-9_]+\)--\>.*?)', r'\1\n', template_text)

    # Preprocessed text output for debugging
#   with open(filename + '.d', 'w') as f:
#       f.write(template_text)

    template = Template(template_text)

    def expand(**args):
        # This function adds one indentation level (4 spaces); it will be used from the template
        args['indent'] = lambda text, idnt = '    ': idnt + text.replace('\n', '\n' + idnt)
        # This function works like enumerate(), telling you whether the current item is the last one
        def enum_last_value(iterable, start=0):
            it = iter(iterable)
            count = start
            last = next(it)
            for val in it:
                yield count, False, last
                last = val
                count += 1
            yield count, True, last
        args['enum_last_value'] = enum_last_value
        return template(**args)

    return expand
