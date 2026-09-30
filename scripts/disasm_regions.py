import argparse

import capstone
import pefile


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("path")
    parser.add_argument("addresses", nargs="+", type=lambda value: int(value, 0))
    parser.add_argument("--count", type=int, default=160)
    args = parser.parse_args()

    pe = pefile.PE(args.path)
    mode = capstone.CS_MODE_32 if pe.FILE_HEADER.Machine == 0x14C else capstone.CS_MODE_64
    md = capstone.Cs(capstone.CS_ARCH_X86, mode)
    md.detail = False
    for address in args.addresses:
        print(f"=== {address:#x} ===")
        rva = address - pe.OPTIONAL_HEADER.ImageBase
        data = pe.get_data(rva, args.count)
        for insn in md.disasm(data, address):
            print(f"{insn.address:#x}: {insn.mnemonic:<8} {insn.op_str}")


if __name__ == "__main__":
    main()
