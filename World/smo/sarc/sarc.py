# Copyright 2018 leoetlino <leo@leolam.fr>
# Licensed under GPLv2+

import ctypes
import io
import json
import math
from operator import itemgetter
import os
import struct

import typing
import zipfile
import tempfile
from ..rstb import rstb

from ..yaz0 import yaz0

def _get_unpack_endian_character(big_endian: bool):
    return '>' if big_endian else '<'

_NUL_CHAR = b'\x00'
_SFAT_NODE_SIZE = 0x10

class SARC:
    """A simple SARC reader."""
    def __init__(self, data: typing.Union[memoryview, bytes]) -> None:
        self._data = memoryview(data)

        # This mirrors what the official library does when reading an archive
        # (sead::SharcArchiveRes::prepareArchive_)

        # Parse the SARC header.
        if self._data[0:4] != b"SARC":
            raise ValueError("Unknown SARC magic")
        bom = self._data[6:8]
        self._be = bom == b"\xFE\xFF"
        if not self._be and bom != b"\xFF\xFE":
            raise ValueError("Invalid BOM")
        version = self._read_u16(0x10)
        if version != 0x100:
            raise ValueError("Unknown SARC version")
        sarc_header_size = self._read_u16(0x4)
        if sarc_header_size != 0x14:
            raise ValueError("Unexpected SARC header size")

        # Parse the SFAT header.
        sfat_header_offset = sarc_header_size
        if self._data[sfat_header_offset:sfat_header_offset+4] != b"SFAT":
            raise ValueError("Unknown SFAT magic")
        sfat_header_size = self._read_u16(sfat_header_offset + 4)
        if sfat_header_size != 0xc:
            raise ValueError("Unexpected SFAT header size")
        node_count = self._read_u16(sfat_header_offset + 6)
        node_offset = sarc_header_size + sfat_header_size
        if (node_count >> 0xe) != 0:
            raise ValueError("Too many entries")

        # Parse the SFNT header.
        sfnt_header_offset = node_offset + _SFAT_NODE_SIZE * node_count
        if self._data[sfnt_header_offset:sfnt_header_offset+4] != b"SFNT":
            raise ValueError("Unknown SNFT magic")
        sfnt_header_size = self._read_u16(sfnt_header_offset + 4)
        if sfnt_header_size != 8:
            raise ValueError("Unexpected SFNT header size")
        name_table_offset = sfnt_header_offset + sfnt_header_size

        # Check the data offset.
        self._data_offset = self._read_u32(0xc)
        if self._data_offset < name_table_offset:
            raise ValueError("File data should not be stored before the name table")

        self._files = self._parse_file_nodes(node_offset, node_count, name_table_offset)

    _Files = typing.Dict[str, typing.Tuple[int, int]]
    def _parse_file_nodes(self, node_offset: int, node_count: int, name_table_offset: int) -> _Files:
        nodes: SARC._Files = dict()

        offset = node_offset
        for i in range(node_count):
            name_hash = self._read_u32(offset)
            flags_and_name_offset = self._read_u32(offset + 4)
            flags = flags_and_name_offset >> 24
            name_offset = flags_and_name_offset & 0xffffff
            file_data_begin = self._read_u32(offset + 8)
            file_data_end = self._read_u32(offset + 0xc)

            if flags_and_name_offset == 0:
                raise ValueError(f"Unnamed files are not supported")
            abs_name_offset = name_table_offset + 4 * name_offset
            if abs_name_offset > self._data_offset:
                raise ValueError(f"Invalid name offset for 0x{name_hash:08x}")

            name = self._read_string(abs_name_offset)
            nodes[name] = (file_data_begin, file_data_end)
            offset += _SFAT_NODE_SIZE

        return nodes

    def guess_default_alignment(self) -> int:
        if len(self._files) <= 2:
            return 4
        gcd = next(iter(self._files.values()))[0] + self._data_offset
        for node in self._files.values():
            gcd = math.gcd(gcd, node[0] + self._data_offset)

        if gcd == 0 or gcd & (gcd - 1) != 0:
            # If the GCD is not a power of 2, the files are mostly likely NOT aligned.
            return 4

        return gcd

    def get_data_offset(self) -> int:
        return self._data_offset

    def get_file_offsets(self) -> typing.List[typing.Tuple[str, int]]:
        offsets: list = []
        for name, node in self._files.items():
            offsets.append((name, node[0]))
        return sorted(offsets, key=itemgetter(1))

    def list_files(self):
        return self._files.keys()

    def is_archive(self, name: str) -> bool:
        node = self._files[name]
        size = node[1] - node[0]
        if size < 4:
            return False

        magic = self._data[self._data_offset + node[0]:self._data_offset + node[0] + 4]
        if magic == b"SARC":
            return True
        if magic == b"Yaz0":
            if size < 0x15:
                return False
            fourcc = self._data[self._data_offset + node[0] + 0x11:self._data_offset + node[0] + 0x15]
            return fourcc == b"SARC"
        return False

    def get_file_data(self, name: str) -> memoryview:
        node = self._files[name]
        return memoryview(self._data[self._data_offset + node[0]:self._data_offset + node[1]])

    def get_file_size(self, name: str) -> int:
        node = self._files[name]
        return node[1] - node[0]

    def get_file_data_offset(self, name: str) -> int:
        return self._files[name][0]

    def extract(self, archive_name: str, print_names=False) -> None:
        name, ext = os.path.splitext(archive_name)
        try: os.mkdir(name)
        except: pass
        self.extract_to_dir(name, print_names)

    def extract_to_dir(self, dest_dir: str, print_names=False) -> None:
        for file_name, node in self._files.items():
            filename = dest_dir + "/" + file_name
            if not os.path.exists(os.path.dirname(filename)):
                os.makedirs(os.path.dirname(filename))
            filedata = self._data[self._data_offset + node[0]:self._data_offset + node[1]]
            if print_names:
                print(filename)
            with open(filename, 'wb') as f:
                f.write(filedata) # type: ignore

    def _read_u16(self, offset: int) -> int:
        return struct.unpack_from(_get_unpack_endian_character(self._be) + 'H', self._data, offset)[0]
    def _read_u32(self, offset: int) -> int:
        return struct.unpack_from(_get_unpack_endian_character(self._be) + 'I', self._data, offset)[0]
    def _read_string(self, offset: int) -> str:
        end = self._data.obj.find(_NUL_CHAR, offset) # type: ignore
        return self._data[offset:end].tobytes().decode()

class _PlaceholderOffsetWriter:
    """Writes a placeholder offset value that will be filled later."""
    def __init__(self, stream: typing.BinaryIO, parent) -> None:
        self._stream = stream
        self._offset = stream.tell()
        self._parent = parent
    def write_placeholder(self) -> None:
        self._stream.write(self._parent._u32(0xffffffff))
    def write_offset(self, offset: int, base: int = 0) -> None:
        current_offset = self._stream.tell()
        self._stream.seek(self._offset)
        self._stream.write(self._parent._u32(offset - base))
        self._stream.seek(current_offset)
    def write_current_offset(self, base: int = 0) -> None:
        self.write_offset(self._stream.tell(), base)

def _align_up(n: int, alignment: int) -> int:
    return (n + alignment - 1) & -alignment

def _load_aglenv_file_info() -> typing.List[dict]:
    if ".apworld" in os.path.realpath(__file__):
        zf = zipfile.ZipFile(os.path.dirname(os.path.realpath(__file__).replace("\\smo\\sarc", "")))

        with tempfile.TemporaryDirectory() as tempdir:
            zf.extractall(tempdir)
            with open(tempdir + '/smo/sarc/aglenv_file_info.json', 'r', encoding='utf-8') as f:
                return json.load(f) # type: ignore
    else:
        with open(os.path.dirname(os.path.realpath(__file__)) + '/aglenv_file_info.json', 'r', encoding='utf-8') as f:
            return json.load(f)  # type: ignore

def _load_botw_resource_factory_info() -> typing.Dict[str, rstb.SizeCalculator.Factory]:
    return rstb.SizeCalculator().get_factory_info()

class SARCWriter:
    _aglenv_file_info = _load_aglenv_file_info()
    _botw_resource_factory_info = _load_botw_resource_factory_info()

    class File(typing.NamedTuple):
        name: str
        data: typing.Union[memoryview, bytes]

    def __init__(self, be: bool) -> None:
        self._be = be
        self._hash_multiplier = 0x65
        self._files: typing.Dict[int, SARCWriter.File] = dict()
        self._alignment: typing.Dict[str, int] = dict()
        self._default_alignment = 4
        self._has_proper_resource_system = True

    def set_align_for_nested_sarc(self, enable: bool) -> None:
        self._has_proper_resource_system = not enable

    def set_has_proper_resource_system(self, has_proper_res_system: bool) -> None:
        self._has_proper_resource_system = has_proper_res_system

    def set_default_alignment(self, value: int) -> None:
        if value == 0 or value & (value - 1) != 0:
            raise ValueError('Alignment must be a non-zero power of 2')
        self._default_alignment = value

    def _refresh_alignment_info(self) -> None:
        self._alignment = dict()
        for entry in self._aglenv_file_info:
            self.add_alignment_requirement(entry['ext'], entry['align'])
            self.add_alignment_requirement(entry['bext'], entry['align'])
        # BotW: Pack/Bootup.pack/Env/env.sgenvb/postfx/*.bksky (AAMP)
        self.add_alignment_requirement('ksky', 8)
        self.add_alignment_requirement('bksky', 8)
        # BotW: Pack/TitleBG.pack/Terrain/System/tera_resource.Nin_NX_NVN.release.ssarc
        self.add_alignment_requirement('gtx', 0x2000)
        self.add_alignment_requirement('sharcb', 0x1000)
        self.add_alignment_requirement('sharc', 0x1000)
        # BotW: Pack/Bootup.pack/Layout/MultiFilter.ssarc/*.baglmf (AAMP)
        self.add_alignment_requirement('baglmf', 0x80)
        # BotW: Event/*.beventpack
        # For some reason, bfevfl are aligned even when they don't need to. But only those
        # that are in beventpacks...
        def has_only_event_packs() -> bool:
            for file in self._files.values():
                if not (file.name.startswith('EventFlow/') and file.name.endswith('.bfevfl')):
                    return False
            return True
        if has_only_event_packs():
            self.add_alignment_requirement('bfevfl', 0x100)
        # BotW: Font/*.bfarc/.bffnt
        self.add_alignment_requirement('bffnt', 0x1000 if not self._be else 0x2000)

    def set_big_endian(self, be: bool) -> None:
        self._be = be

    def add_alignment_requirement(self, extension_without_dot: str, alignment: int) -> None:
        self._alignment[extension_without_dot] = abs(alignment)

    def _get_file_alignment_for_sarc(self, file: File) -> int:
        if self._has_proper_resource_system:
            return 0
        data = memoryview(file.data)
        if len(data) <= 0x4:
            return 0
        if data[0:4] == b'Yaz0' and data[0x11:0x15] == b'SARC':
            data = memoryview(yaz0.DecompressYaz(data))
        if data[0:4] != b'SARC':
            return 0
        # In some archives (SMO for example), Nintendo seems to use a somewhat arbitrary
        # alignment requirement (0x2000) for nested SARCs.
        return 0x2000

    def _get_file_alignment_for_new_binary_file(self, file: File) -> int:
        """Detects alignment requirements for binary files with new nn::util::BinaryFileHeader."""
        if len(file.data) <= 0x20:
            return 0
        bom = file.data[0xc:0xc+2]
        if bom != b'\xff\xfe' and bom != b'\xfe\xff':
            return 0

        be = bom == b'\xfe\xff'
        file_size: int = struct.unpack_from(_get_unpack_endian_character(be) + 'I', file.data, 0x1c)[0]
        if len(file.data) != file_size:
            return 0
        return 1 << file.data[0xe]

    def _get_file_alignment_for_old_bflim(self, file: File) -> int:
        # XXX: should another flag be added for the platform?
        if not self._be:
            return 0
        if len(file.data) <= 0x28 or file.data[-0x28:-0x24] != b'FLIM':
            return 0
        return struct.unpack('>H', file.data[-0x8:-0x6])[0]

    def _get_alignment_for_file_data(self, file: File) -> int:
        ext = os.path.splitext(file.name)[1][1:]
        alignment = self._alignment.get(ext, self._default_alignment)
        alignment = max(alignment, self._get_file_alignment_for_sarc(file))
        if not self._has_proper_resource_system or ext not in self._botw_resource_factory_info:
            alignment = max(alignment, self._get_file_alignment_for_new_binary_file(file))
            alignment = max(alignment, self._get_file_alignment_for_old_bflim(file))
        return alignment

    def _hash_file_name(self, name: str) -> int:
        #
        # The algorithm looks like this in C++:
        #
        #       uint32_t hash = 0;
        #       int i = 0;
        #       while (true) {
        #         char c = string[i++];
        #         if (!c)
        #           break;
        #         hash = hash * multiplier + c;
        #       }
        #
        # There are three things we need to be careful about when reimplementing it in Python:
        # * the hash is kept in an unsigned 32-bit integer variable,
        #   thus standard integer arithmetic (wrapping, signedness, etc.) applies;
        # * the character is stored in a char variable. Char signedness is implementation-defined
        #   but Nintendo's tools use signed chars;
        # * the string is iterated byte per byte, not Unicode-character-wise.
        #
        h = 0
        for b in name.encode():
            c = ctypes.c_int8(b).value
            h = (c + (h * self._hash_multiplier) & 0xffffffff) & 0xffffffff
        return h

    def add_file(self, name: str, data: typing.Union[memoryview, bytes]) -> None:
        self._files[self._hash_file_name(name)] = SARCWriter.File(name, data)

    def delete_file(self, name: str) -> None:
        del self._files[self._hash_file_name(name)]

    def get_file_offsets(self) -> typing.List[typing.Tuple[str, int]]:
        self._refresh_alignment_info()
        offsets: list = []
        data_offset = 0
        for h in sorted(self._files.keys()):
            alignment = self._get_alignment_for_file_data(self._files[h])
            data_offset = _align_up(data_offset, alignment)
            offsets.append((self._files[h].name, data_offset))
            data_offset += len(self._files[h].data)
        return offsets

    def get_bytes(self):
        stream = io.BytesIO()
        self.write(stream)
        return stream.getvalue()

    def write(self, stream: typing.BinaryIO) -> int:
        self._refresh_alignment_info()

        # SARC header
        stream.write(b'SARC')
        stream.write(self._u16(0x14))
        stream.write(self._u16(0xfeff))
        file_size_writer = self._write_placeholder_offset(stream)
        data_offset_writer = self._write_placeholder_offset(stream)
        stream.write(self._u16(0x100))
        stream.write(self._u16(0)) # Unused.

        # SFAT header
        stream.write(b'SFAT')
        stream.write(self._u16(0xc))
        stream.write(self._u16(len(self._files)))
        stream.write(self._u32(self._hash_multiplier))

        # Node information
        sorted_hashes = sorted(self._files.keys())
        file_alignments: typing.List[int] = []
        string_offset = 0
        data_offset = 0
        # Some files have specific alignment requirements. These must be satisfied by
        # aligning file offsets *and* the data offset to the maximum alignment value
        # since file offsets are always relative to the data offset.
        data_offset_alignment = 1
        for h in sorted_hashes:
            stream.write(self._u32(h))
            stream.write(self._u32(0x01000000 | (string_offset >> 2)))
            alignment = self._get_alignment_for_file_data(self._files[h])
            data_offset_alignment = max(data_offset_alignment, alignment)
            file_alignments.append(alignment)
            data_offset = _align_up(data_offset, alignment)
            stream.write(self._u32(data_offset))
            data_offset += len(self._files[h].data)
            stream.write(self._u32(data_offset))
            string_offset += _align_up(len(self._files[h].name.encode()) + 1, 4)

        # File name table
        stream.write(b'SFNT')
        stream.write(self._u16(8))
        stream.write(self._u16(0))
        for h in sorted_hashes:
            stream.write(self._files[h].name.encode())
            stream.write(_NUL_CHAR)
            stream.seek(_align_up(stream.tell(), 4))

        # File data
        stream.seek(_align_up(stream.tell(), data_offset_alignment))
        for i, h in enumerate(sorted_hashes):
            stream.seek(_align_up(stream.tell(), file_alignments[i]))
            if i == 0:
                data_offset_writer.write_current_offset()
            stream.write(self._files[h].data) # type: ignore

        # Write the final file size.
        file_size_writer.write_current_offset()
        return data_offset_alignment

    def _write_placeholder_offset(self, stream) -> _PlaceholderOffsetWriter:
        p = _PlaceholderOffsetWriter(stream, self)
        p.write_placeholder()
        return p

    def _u16(self, value: int) -> bytes:
        return struct.pack(_get_unpack_endian_character(self._be) + 'H', value)
    def _u32(self, value: int) -> bytes:
        return struct.pack(_get_unpack_endian_character(self._be) + 'I', value)

def read_file_and_make_sarc(f: typing.BinaryIO) -> typing.Optional[SARC]:
    f.seek(0)
    magic: bytes = f.read(4)
    if magic == b"Yaz0":
        f.seek(0x11)
        first_data_group_fourcc: bytes = f.read(4)
        f.seek(0)
        if first_data_group_fourcc != b"SARC":
            return None
        data = yaz0.DecompressYaz(f.read())
    elif magic == b"SARC":
        f.seek(0)
        data = f.read()
    else:
        return None
    return SARC(data)

def make_writer_from_sarc(sarc: SARC, filter_fn: typing.Optional[typing.Callable[[str], bool]] = None) -> typing.Optional[SARCWriter]:
    writer = SARCWriter(be=sarc._be)
    writer.set_default_alignment(sarc.guess_default_alignment())
    for file in sarc.list_files():
        if not filter_fn or filter_fn(file):
            writer.add_file(file, sarc.get_file_data(file))

    return writer

def read_sarc_and_make_writer(f: typing.BinaryIO, filter_fn: typing.Optional[typing.Callable[[str], bool]] = None) -> typing.Optional[SARCWriter]:
    sarc = read_file_and_make_sarc(f)
    if not sarc:
        return None
    return make_writer_from_sarc(sarc, filter_fn)
