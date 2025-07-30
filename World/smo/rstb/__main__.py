# Copyright 2018 leoetlino <leo@leolam.fr>
# Licensed under GPLv2+

import argparse
import sys

from . import rstb
from . import util

def parse_size(size_str: str, be: bool, force: bool) -> int:
    try:
        return int(size_str, 0)
    except ValueError:
        size = rstb.SizeCalculator().calculate_file_size(file_name=size_str, wiiu=be, force=force)
        if not size:
            sys.stderr.write('error: could not calculate size because the resource factory allocates extra memory depending on the resource contents\n')
            sys.stderr.write('Unless you know what you are doing, it is recommended to just delete the entry from the RSTB for now.\n')
            sys.exit(2)
        return size

def rstb_get(args, table: rstb.ResourceSizeTable) -> None:
    size = table.get_size(args.name)
    if size != 0:
        print('%s: %d bytes (0x%08x)' % (args.name, size, size))
    else:
        sys.stderr.write('%s: not in table\n' % args.name)
        sys.exit(2)

def rstb_del(args, table: rstb.ResourceSizeTable) -> None:
    if not table.is_in_table(args.name):
        sys.stderr.write('%s: not in table\n' % args.name)
        sys.exit(1)
    table.delete_entry(args.name)
    util.write_rstb(table, args.rstb, args.be)

def rstb_set(args, table: rstb.ResourceSizeTable) -> None:
    if not table.is_in_table(args.name):
        sys.stderr.write('%s: not in table\n' % args.name)
        sys.exit(1)

    old_size = table.get_size(args.name)
    new_size = parse_size(args.size, args.be, force=args.force)
    print('%s: current size is %d bytes (0x%08x)' % (args.name, old_size, old_size))
    print('%s: new size is %d bytes (0x%08x)' % (args.name, new_size, new_size))
    table.set_size(args.name, new_size)
    util.write_rstb(table, args.rstb, args.be)

def rstb_add(args, table: rstb.ResourceSizeTable) -> None:
    if table.is_in_table(args.name):
        sys.stderr.write('%s: already in table\n' % args.name)
        sys.exit(3)

    new_size = parse_size(args.size, args.be, args.force)
    print('%s: new size is %d bytes (0x%08x)' % (args.name, new_size, new_size))
    table.set_size(args.name, new_size)
    util.write_rstb(table, args.rstb, args.be)

def rstb_compare(args, table: rstb.ResourceSizeTable) -> None:
    if not table.is_in_table(args.name):
        sys.stderr.write('%s: not in table\n' % args.name)
        sys.exit(1)

    listed_size = table.get_size(args.name)
    calculated_size = parse_size(args.file, be=args.be, force=True)
    print('%s:     listed size is %d bytes (0x%08x)' % (args.name, listed_size, listed_size))
    print('%s: calculated size is %d bytes (0x%08x)' % (args.name, calculated_size, calculated_size))

def rstb_dump(args, table: rstb.ResourceSizeTable) -> None:
    print(f'{len(table.crc32_map)} entries in CRC32 map')
    print(f'{len(table.name_map)} entries in name map')
    print()
    for crc32, size in table.crc32_map.items():
        print(f'c {crc32:08x} {size}')
    for name, size in table.name_map.items():
        print(f'n {name:128s} {size}')

def main() -> None:
    parser = argparse.ArgumentParser(description='A tool to manipulate the RSTB (Resource Size TaBle).')
    parser.add_argument('rstb', help='Path to a Breath of the Wild RSTB (yaz0 compressed)')
    parser.add_argument('-b', '--be', action='store_true', help='Use big endian. Defaults to false.')

    subparsers = parser.add_subparsers(dest='command', help='Command')
    subparsers.required = True

    get_parser = subparsers.add_parser('get', description='Get a resource size from the table')
    get_parser.add_argument('name', help='Resource name')
    get_parser.set_defaults(func=rstb_get)

    set_parser = subparsers.add_parser('set', description='Edit an existing entry in the table')
    set_parser.add_argument('name', help='Resource name')
    set_parser.add_argument('size', help='Resource size (integer or file path for auto size detection)')
    set_parser.set_defaults(func=rstb_set)

    add_parser = subparsers.add_parser('add', description='Add a new entry to the table')
    add_parser.add_argument('name', help='Resource name')
    add_parser.add_argument('size', help='Resource size (integer or file path for auto size detection)')
    add_parser.set_defaults(func=rstb_add)

    del_parser = subparsers.add_parser('del', description='Delete a resource from the table')
    del_parser.add_argument('name', help='Resource name')
    del_parser.set_defaults(func=rstb_del)

    calc_parser = subparsers.add_parser('compare', description='Compare a resource size with its listed size in the RSTB')
    calc_parser.add_argument('name', help='Resource name')
    calc_parser.add_argument('file', help='Host file path to resource')
    calc_parser.set_defaults(func=rstb_compare)

    dump_parser = subparsers.add_parser('dump', description='Dump entries to a text format')
    dump_parser.set_defaults(func=rstb_dump)

    for p in (add_parser, set_parser):
        p.add_argument('--force', action='store_true', help='Do not warn when the resource size cannot be determined because the factory is complex. WARNING: this will likely cause the game to crash.')

    args = parser.parse_args()
    table = util.read_rstb(args.rstb, args.be)
    args.func(args, table)

if __name__ == '__main__':
    main()
