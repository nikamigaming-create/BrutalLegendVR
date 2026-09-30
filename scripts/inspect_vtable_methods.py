import pefile

pe = pefile.PE(r'D:\SteamLibrary\steamapps\common\BrutalLegend\BrutalLegend.exe')
image_base = pe.OPTIONAL_HEADER.ImageBase

def find_str_containing(sub):
    sub_b = sub.encode('ascii')
    for s in pe.sections:
        d = s.get_data()
        idx = d.find(sub_b)
        count = 0
        while idx != -1 and count < 10:
            # find null terminators before and after
            start = d.rfind(b'\x00', 0, idx) + 1
            end = d.find(b'\x00', idx)
            st = d[start:end].decode('ascii', errors='ignore')
            print(f'Found "{st}" at {hex(image_base + s.VirtualAddress + start)}')
            idx = d.find(sub_b, idx + 1)
            count += 1

print('Searching for bone / skeleton / mesh strings...')
find_str_containing('skeleton')
find_str_containing('head')
find_str_containing('eddie')
