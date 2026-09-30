import pefile

pe = pefile.PE(r'D:\SteamLibrary\steamapps\common\BrutalLegend\BrutalLegend.exe')
image_base = pe.OPTIONAL_HEADER.ImageBase

def read_u32(va):
    rva = va - image_base
    return int.from_bytes(pe.get_data(rva, 4), 'little')

# In MSVC, vtable is preceded by CompleteObjectLocator pointer at vtable - 4
vtable_va = 0xe9881c
# Let's see vtable start
print(f'Vtable around {hex(vtable_va)}:')
# Search backwards for CompleteObjectLocator
for va in range(0xe98700, 0xe98830, 4):
    col = read_u32(va - 4)
    if col >= image_base and col < image_base + pe.OPTIONAL_HEADER.SizeOfImage:
        # Check if col looks like CompleteObjectLocator
        # signature is usually 0
        try:
            rva_col = col - image_base
            sig = int.from_bytes(pe.get_data(rva_col, 4), 'little')
            offset = int.from_bytes(pe.get_data(rva_col + 4, 4), 'little')
            cdOffset = int.from_bytes(pe.get_data(rva_col + 8, 4), 'little')
            pTypeDesc = int.from_bytes(pe.get_data(rva_col + 12, 4), 'little')
            if pTypeDesc >= image_base and pTypeDesc < image_base + pe.OPTIONAL_HEADER.SizeOfImage:
                # Read type descriptor name at pTypeDesc + 8
                name = pe.get_data(pTypeDesc - image_base + 8, 64).split(b'\x00')[0].decode('ascii', errors='ignore')
                print(f'Vtable at {hex(va)}, COL at {hex(col)}: Class name: {name}')
        except Exception as e:
            pass
