import pefile
import capstone

pe = pefile.PE(r'D:\SteamLibrary\steamapps\common\BrutalLegend\BrutalLegend.exe')
image_base = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

def disasm_va(va, length=100):
    rva = va - image_base
    for ins in md.disasm(pe.get_data(rva, length), va):
        print(f'{hex(ins.address)}: {ins.mnemonic} {ins.op_str}')

# Look at 0x966ed0 (RVA_GET_JOINT_TRANSFORM) again
# What is the joint structure?
# [ecx + 0x30] was called in 0xd66e40:
# mov eax, dword ptr [ecx + 0x30]
# mov eax, dword ptr [eax + 0x18]
# add eax, 0x30
# ret

# Let's see who creates or manages joints: search for 0xd66e40
text_sec = pe.sections[0]
d = text_sec.get_data()
target_va = 0xd66e40
for i in range(len(d) - 5):
    if d[i] == 0xe8:
        rel = int.from_bytes(d[i+1:i+5], 'little', signed=True)
        dest = image_base + text_sec.VirtualAddress + i + 5 + rel
        if dest == target_va:
            print(f'Call to 0xd66e40 from {hex(image_base + text_sec.VirtualAddress + i)}')
