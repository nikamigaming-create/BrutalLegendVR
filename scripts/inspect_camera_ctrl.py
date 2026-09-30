import pefile
import capstone

pe = pefile.PE(r'D:\SteamLibrary\steamapps\common\BrutalLegend\BrutalLegend.exe')
image_base = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

def disasm_va(va, length=120):
    rva = va - image_base
    for ins in md.disasm(pe.get_data(rva, length), va):
        print(f'{hex(ins.address)}: {ins.mnemonic} {ins.op_str}')

print('--- 0x580b10 to 0x580b50 ---')
disasm_va(0x580b10, 60)
