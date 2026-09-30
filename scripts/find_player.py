import pefile

pe = pefile.PE(r'D:\SteamLibrary\steamapps\common\BrutalLegend\BrutalLegend.exe')
image_base = pe.OPTIONAL_HEADER.ImageBase

for s in pe.sections:
    d = s.get_data()
    for term in [b'PlayerCharacter', b'player_character', b'LocalPlayer', b'Hero', b'Eddie', b'Ophelia']:
        idx = d.find(term)
        count = 0
        while idx != -1 and count < 8:
            va = image_base + s.VirtualAddress + idx
            end = d.find(b'\x00', idx)
            st = d[idx:end].decode('ascii', errors='ignore')
            print(f'{term.decode()} at {hex(va)}: {st}')
            idx = d.find(term, idx + 1)
            count += 1
