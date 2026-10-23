#!/usr/bin/env python3
"""Offline profile/source contract checks for the retail SA vehicle-surface hooks.

This reads the supplied executable as data only; it never loads or executes it.
Invoke explicitly with the local private executable, e.g.:
  python vehicle_surface_audio_profile.py --exe <private-path-to-gta_sa.exe>
"""

from __future__ import annotations

import argparse
import re
import struct
from pathlib import Path


def pe_image(path: Path):
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise AssertionError("input is not a PE image")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise AssertionError("invalid PE signature")
    section_count = struct.unpack_from("<H", data, pe_offset + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
    optional = pe_offset + 24
    if struct.unpack_from("<H", data, optional)[0] != 0x10B:
        raise AssertionError("expected 32-bit PE")
    image_base = struct.unpack_from("<I", data, optional + 28)[0]
    section_table = optional + optional_size
    sections = []
    for index in range(section_count):
        offset = section_table + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<IIII", data, offset + 8
        )
        sections.append(
            (image_base + virtual_address, image_base + virtual_address + virtual_size,
             raw_offset, raw_size)
        )

    def read_va(address: int, size: int) -> bytes:
        for start, end, raw_offset, raw_size in sections:
            if start <= address and address + size <= end:
                offset = raw_offset + address - start
                if address - start + size > raw_size:
                    break
                return data[offset : offset + size]
        raise AssertionError(f"VA 0x{address:08X} is not backed by PE data")

    return read_va


def call_target(read_va, site: int) -> int:
    instruction = read_va(site, 5)
    if instruction[0] != 0xE8:
        raise AssertionError(f"0x{site:08X} is no longer a CALL rel32")
    relative = struct.unpack_from("<i", instruction, 1)[0]
    return site + 5 + relative


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--exe',required=True,type=Path)
    args=parser.parse_args()
    read=pe_image(args.exe)
    source=(Path(__file__).resolve().parents[1]/'src/sa_vehicle_surface_audio.cpp').read_text()
    table=source.split('constexpr Call calls[]={',1)[1].split('};',1)[0]
    sites=re.findall(r'\{(0x[0-9A-Fa-f]+),(0x[0-9A-Fa-f]+),[012],(?:true|false)\}',table)
    assert len(sites)==12
    for site,target in sites:
        assert call_target(read,int(site,16))==int(target,16),(site,target)
    assert 'owned[Family]?-1000.0f:volume' in source
    assert 'reinterpret_cast<Process>(targets[Family])(self,params)' in source
    assert 'previous_ground=*reinterpret_cast<const uint8_t*>(v+(bike?0x806:0x962))' in source
    assert 'transmission+0x60' in source
    print('PASS: 12 vehicle road/skid/reverse CALLs and native lifecycle forwarding; gameExecuted=false')
if __name__=='__main__':main()
