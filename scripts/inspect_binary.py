import pefile
import capstone

pe = pefile.PE(r'D:\SteamLibrary\steamapps\common\BrutalLegend\BrutalLegend.exe')
image_base = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

def disasm_va(va, length=80):
    rva = va - image_base
    for ins in md.disasm(pe.get_data(rva, length), va):
        print(f'{hex(ins.address)}: {ins.mnemonic} {ins.op_str}')

# Find where 0x8ed74c is located - what function is that?
# Let's see the function start for 0x8ed74c
rdata = pe.get_section_by_rva(0x4ed74c)
# search backwards for function prologue push ebp; mov ebp, esp (55 8b ec)
data = pe.get_data(0x4ed500, 0x300)
for i in range(len(data)-3):
    if data[i] == 0x55 and data[i+1] == 0x8b and data[i+2] == 0xec:
        print(f'Prologue at {hex(0x4ed500 + i + image_base)}')

print('--- Function containing 0x8ed74c ---')
disasm_va(0x8ed530, 100)
