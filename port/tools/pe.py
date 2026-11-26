"""Minimal read-only PE32 reader for pulling data out of the original speed.exe."""
import struct


class PE:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        e_lfanew = struct.unpack_from("<I", self.data, 0x3C)[0]
        nsec = struct.unpack_from("<H", self.data, e_lfanew + 6)[0]
        optsize = struct.unpack_from("<H", self.data, e_lfanew + 20)[0]
        self.image_base = struct.unpack_from("<I", self.data, e_lfanew + 24 + 28)[0]
        self.sections = []
        off = e_lfanew + 24 + optsize
        for i in range(nsec):
            name = self.data[off:off + 8].rstrip(b"\0").decode()
            vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", self.data, off + 8)
            self.sections.append((name, self.image_base + va, vsize, rawptr, rawsize))
            off += 40

    def section_of(self, va):
        for s in self.sections:
            if s[1] <= va < s[1] + max(s[2], s[4]):
                return s
        return None

    def read(self, va, n):
        s = self.section_of(va)
        if s is None:
            raise ValueError(f"{va:#x} not in image")
        name, sva, vsize, rawptr, rawsize = s
        rel = va - sva
        out = self.data[rawptr + rel:rawptr + min(rel + n, rawsize)]
        return out + b"\0" * (n - len(out))  # .bss tail reads as zero

    def u32(self, va):
        return struct.unpack("<I", self.read(va, 4))[0]

    def cstr(self, va):
        s = self.section_of(va)
        name, sva, vsize, rawptr, rawsize = s
        start = rawptr + (va - sva)
        return self.data[start:self.data.index(b"\0", start)].decode("latin-1")
