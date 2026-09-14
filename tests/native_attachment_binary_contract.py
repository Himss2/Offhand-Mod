import hashlib
import struct
import sys
from pathlib import Path

EXPECTED_SHA256='444e77434bdd3789a0d90978d06336a99831e78e52955e528258cc375dfa0557'

def load_virtual_bytes(binary:bytes,address:int,size:int)->bytes:
    assert binary[:4]==b'\x7fELF' and binary[4]==2 and binary[5]==1
    phoff=struct.unpack_from('<Q',binary,0x20)[0]
    phentsize=struct.unpack_from('<H',binary,0x36)[0]
    phnum=struct.unpack_from('<H',binary,0x38)[0]
    for i in range(phnum):
        off=phoff+i*phentsize
        p_type,p_flags,p_offset,p_vaddr=struct.unpack_from('<IIQQ',binary,off)
        p_filesz=struct.unpack_from('<Q',binary,off+0x20)[0]
        if p_type==1 and p_vaddr<=address and address+size<=p_vaddr+p_filesz:
            start=p_offset+address-p_vaddr
            return binary[start:start+size]
    raise AssertionError(f'RVA 0x{address:X} outside file-backed PT_LOAD')

def branch_target(binary:bytes,address:int,link:bool)->int:
    ins=int.from_bytes(load_virtual_bytes(binary,address,4),'little')
    opcode=0x94000000 if link else 0x14000000
    assert ins&0xFC000000==opcode,f'0x{address:X} is not {"BL" if link else "B"}'
    imm=ins&0x03FFFFFF
    if imm&0x02000000: imm-=0x04000000
    return address+imm*4

def executable_segments(binary:bytes):
    phoff=struct.unpack_from('<Q',binary,0x20)[0]
    phentsize=struct.unpack_from('<H',binary,0x36)[0]
    phnum=struct.unpack_from('<H',binary,0x38)[0]
    for i in range(phnum):
        off=phoff+i*phentsize
        p_type,p_flags,p_offset,p_vaddr=struct.unpack_from('<IIQQ',binary,off)
        p_filesz=struct.unpack_from('<Q',binary,off+0x20)[0]
        if p_type==1 and (p_flags&1):
            yield p_offset,p_vaddr,p_filesz

def direct_bl_callers(binary:bytes,target:int):
    callers=[]
    for file_off,vaddr,size in executable_segments(binary):
        end=file_off+size-(size%4)
        for o in range(file_off,end,4):
            ins=int.from_bytes(binary[o:o+4],'little')
            if ins&0xFC000000!=0x94000000:
                continue
            imm=ins&0x03FFFFFF
            if imm&0x02000000: imm-=0x04000000
            addr=vaddr+(o-file_off)
            if addr+imm*4==target:
                callers.append(addr)
    return callers

def main():
    assert len(sys.argv)==2,'usage: native_attachment_binary_contract.py LIB'
    binary=Path(sys.argv[1]).read_bytes()
    assert hashlib.sha256(binary).hexdigest()==EXPECTED_SHA256

    # Attachment slot routing and exact native-only boundaries.
    assert load_virtual_bytes(binary,0x9B36358,4)==bytes.fromhex('c8008052')
    assert branch_target(binary,0x9B36314,True)==0x9B368D4
    assert branch_target(binary,0x9B36370,True)==0x9B368D4
    assert branch_target(binary,0x9B36A18,False)==0x9B3A228
    assert branch_target(binary,0xA2C837C,True)==0x9B36A80
    assert load_virtual_bytes(binary,0xA2C8374,8)==bytes.fromhex(
        'e5031f2a26008052'
    ),'prepare caller must pass w5=0, w6=1'
    assert branch_target(binary,0x9B3779C,True)==0xAF3A1E4
    assert branch_target(binary,0x9B37814,True)==0xAF3A1E4
    assert branch_target(binary,0x9B254C8,True)==0xF147ED0

    # 9B368D4 itself computes variable.is_first_person before tail-drawing.
    assert branch_target(binary,0x9B36908,True)==0xEC8A478
    assert branch_target(binary,0x9B36930,True)==0xEE63508
    assert branch_target(binary,0x9B36934,True)==0xEEA721C
    assert load_virtual_bytes(binary,0x2652B1D,25)==b'variable.is_first_person\x00'
    assert direct_bl_callers(binary,0x9B368D4)==[
        0x9B361AC,0x9B361D8,0x9B36204,0x9B36230,
        0x9B3625C,0x9B36314,0x9B36370,
    ]
    assert direct_bl_callers(binary,0xAF3A1E4)==[0x9B3779C,0x9B37814]

    fingerprints={
        0x9B36A80:'fd7bbaa9fc6f01a9fa6702a9f85f03a9',
        0xAF3A1E4:'ff4302d1fd7b03a9fc6f04a9fa6705a9',
        0x9B368D4:'fd7bbaa9fc6f01a9fa6702a9f85f03a9',
        0xF147ED0:'08784339a8000034008441ad028c42ad',
        0xEC8A478:'fd7bbfa9fd030091090840f92a839452',
        0xEE63508:'ffc300d1fd7b01a9f44f02a9fd430091',
        0xEEA721C:'00200091c0035fd6',
    }
    for address,hex_bytes in fingerprints.items():
        expected=bytes.fromhex(hex_bytes)
        assert load_virtual_bytes(binary,address,len(expected))==expected,(
            f'entry fingerprint changed at 0x{address:X}'
        )

    # Native query.item_slot_to_bone_name already maps off_hand -> leftitem.
    assert load_virtual_bytes(binary,0xEE89174,28).hex()==(
        '88f195d2010040f973c205916874a5f22850c4f288a9ebf23f0008eb'
    )
    assert load_virtual_bytes(binary,0xEE89294,32).hex()==(
        '00e4006fe15e92d2080080126115b6f2e80300b961b9dff2619ee3f2e083803c'
    )
    # Tiny HashedString accessor remains only data evidence; never inline-hook it.
    assert load_virtual_bytes(binary,0xEEAB3AC,12).hex()==(
        '00200091c0035fd6085040b9'
    )

    # F147ED0 copies the native owner matrix when +0xDE is set, then continues
    # into local pose reads. Therefore the new patch must mutate only +0x70 pose.
    expected_ins={
        0xF147ED0:'08784339',0xF147ED4:'a8000034',
        0xF147ED8:'008441ad',0xF147EDC:'028c42ad',
        0xF147EE0:'400400ad',0xF147EE4:'420c01ad',
        0xF147EEC:'0204476d',0xF147FF0:'01c047fc',0xF147FF4:'024048fc',
    }
    for address,hex_bytes in expected_ins.items():
        assert load_virtual_bytes(binary,address,4)==bytes.fromhex(hex_bytes),(
            f'compose/local-pose instruction changed at 0x{address:X}'
        )

    print(
        'native attachment binary contract passed: exact 1.26.45.1 Bow owner '
        'resolver + native Trident FPP route/local-pose pipeline'
    )

if __name__=='__main__':
    main()
