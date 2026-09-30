import pefile

pe = pefile.PE(r'D:\SteamLibrary\steamapps\common\BrutalLegend\BrutalLegend.exe')
image_base = pe.OPTIONAL_HEADER.ImageBase

def find_str_refs(text):
    b = text.encode('ascii')
    for s in pe.sections:
        d = s.get_data()
        idx = d.find(b)
        while idx != -1:
            va = image_base + s.VirtualAddress + idx
            # find references to this va
            va_b = va.to_bytes(4, 'little')
            for s2 in pe.sections:
                d2 = s2.get_data()
                idx2 = d2.find(va_b)
                while idx2 != -1:
                    va2 = image_base + s2.VirtualAddress + idx2
                    print(f'{text} ({hex(va)}) referenced from {hex(va2)}')
                    idx2 = d2.find(va_b, idx2 + 1)
            idx = d.find(b, idx + 1)

find_str_refs('.?AVCoRenderMesh@@')
