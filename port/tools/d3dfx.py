#!/usr/bin/env python3
"""Dump compiled Direct3D 9 effects (fx_2_0 binaries) as readable listings.

For each effect this prints the parameters (with default values), the
techniques/passes and their states, and a disassembly of every shader token
stream with its constant table (CTAB) so registers can be mapped back to
parameter names ("c4..c7 = WorldViewProj").  Preshader expressions (FXLC,
used by the effect to compute render states from parameters) are decoded too.

Usage:
    python3 port/tools/d3dfx.py <file.fx> [more.fx ...] [--out DIR]

The effect layout follows the format d3dx9 itself reads (see Wine's
dlls/d3dx9_36/effect.c): header (0xFEFF0901, size of data blob), data blob
(names, typedefs, values; all offsets are relative to it), then parameter,
technique, object and resource tables.  Shader blobs live in the object and
resource tables; a brute-force scan of the file for shader token streams is
also done as a cross-check.
"""
import argparse
import os
import struct
import sys

FX_TAG = 0xFEFF0901

# ---------------------------------------------------------------------------
# Effect parameter types / classes (D3DXPARAMETER_TYPE / _CLASS)

PT_NAMES = {
    0: "void", 1: "bool", 2: "int", 3: "float", 4: "string", 5: "texture",
    6: "texture1D", 7: "texture2D", 8: "texture3D", 9: "textureCUBE",
    10: "sampler", 11: "sampler1D", 12: "sampler2D", 13: "sampler3D",
    14: "samplerCUBE", 15: "pixelshader", 16: "vertexshader",
    17: "pixelfragment", 18: "vertexfragment", 19: "unsupported",
}
PC_SCALAR, PC_VECTOR, PC_MATRIX_ROWS, PC_MATRIX_COLUMNS, PC_OBJECT, PC_STRUCT = range(6)
SAMPLER_TYPES = (10, 11, 12, 13, 14)
PT_PIXELSHADER, PT_VERTEXSHADER = 15, 16


def _rs_table():
    """Effect state operation -> name (d3dx9 order, as in Wine's state_table)."""
    t = {}
    rs = ["ZENABLE", "FILLMODE", "SHADEMODE", "ZWRITEENABLE", "ALPHATESTENABLE",
          "LASTPIXEL", "SRCBLEND", "DESTBLEND", "CULLMODE", "ZFUNC", "ALPHAREF",
          "ALPHAFUNC", "DITHERENABLE", "ALPHABLENDENABLE", "FOGENABLE",
          "SPECULARENABLE", "FOGCOLOR", "FOGTABLEMODE", "FOGSTART", "FOGEND",
          "FOGDENSITY", "RANGEFOGENABLE", "STENCILENABLE", "STENCILFAIL",
          "STENCILZFAIL", "STENCILPASS", "STENCILFUNC", "STENCILREF",
          "STENCILMASK", "STENCILWRITEMASK", "TEXTUREFACTOR"]
    rs += ["WRAP%d" % i for i in range(16)]
    rs += ["CLIPPING", "LIGHTING", "AMBIENT", "FOGVERTEXMODE", "COLORVERTEX",
           "LOCALVIEWER", "NORMALIZENORMALS", "DIFFUSEMATERIALSOURCE",
           "SPECULARMATERIALSOURCE", "AMBIENTMATERIALSOURCE",
           "EMISSIVEMATERIALSOURCE", "VERTEXBLEND", "CLIPPLANEENABLE",
           "POINTSIZE", "POINTSIZE_MIN", "POINTSIZE_MAX", "POINTSPRITEENABLE",
           "POINTSCALEENABLE", "POINTSCALE_A", "POINTSCALE_B", "POINTSCALE_C",
           "MULTISAMPLEANTIALIAS", "MULTISAMPLEMASK", "PATCHEDGESTYLE",
           "DEBUGMONITORTOKEN", "INDEXEDVERTEXBLENDENABLE", "COLORWRITEENABLE",
           "TWEENFACTOR", "BLENDOP", "POSITIONDEGREE", "NORMALDEGREE",
           "SCISSORTESTENABLE", "SLOPESCALEDEPTHBIAS", "ANTIALIASEDLINEENABLE",
           "MINTESSELLATIONLEVEL", "MAXTESSELLATIONLEVEL", "ADAPTIVETESS_X",
           "ADAPTIVETESS_Y", "ADAPTIVETESS_Z", "ADAPTIVETESS_W",
           "ENABLEADAPTIVETESSELLATION", "TWOSIDEDSTENCILMODE",
           "CCW_STENCILFAIL", "CCW_STENCILZFAIL", "CCW_STENCILPASS",
           "CCW_STENCILFUNC", "COLORWRITEENABLE1", "COLORWRITEENABLE2",
           "COLORWRITEENABLE3", "BLENDFACTOR", "SRGBWRITEENABLE", "DEPTHBIAS",
           "SEPARATEALPHABLENDENABLE", "SRCBLENDALPHA", "DESTBLENDALPHA",
           "BLENDOPALPHA"]
    for i, n in enumerate(rs):
        t[i] = n
    tss = ["COLOROP", "COLORARG0", "COLORARG1", "COLORARG2", "ALPHAOP",
           "ALPHAARG0", "ALPHAARG1", "ALPHAARG2", "RESULTARG", "BUMPENVMAT00",
           "BUMPENVMAT01", "BUMPENVMAT10", "BUMPENVMAT11", "TEXCOORDINDEX",
           "BUMPENVLSCALE", "BUMPENVLOFFSET", "TEXTURETRANSFORMFLAGS",
           "CONSTANT"]
    base = len(rs)  # 0x67
    for i, n in enumerate(tss):
        t[base + i] = "TSS_" + n
    t[base + len(tss)] = "NPATCHMODE"      # 0x79
    t[base + len(tss) + 1] = "FVF"         # 0x7a
    for i, n in enumerate(["WORLD", "VIEW", "PROJECTION", "TEXTURE"]):
        t[0x7b + i] = "TRANSFORM_" + n
    for i, n in enumerate(["DIFFUSE", "AMBIENT", "SPECULAR", "EMISSIVE", "POWER"]):
        t[0x7f + i] = "MATERIAL_" + n
    for i, n in enumerate(["TYPE", "DIFFUSE", "SPECULAR", "AMBIENT", "POSITION",
                           "DIRECTION", "RANGE", "FALLOFF", "ATTENUATION0",
                           "ATTENUATION1", "ATTENUATION2", "THETA", "PHI"]):
        t[0x84 + i] = "LIGHT_" + n
    t[0x91] = "LIGHTENABLE"
    t[0x92] = "VertexShader"
    t[0x93] = "PixelShader"
    for i, n in enumerate(["F", "B", "I", "", "1", "2", "3", "4"]):
        t[0x94 + i] = "VertexShaderConstant" + n
        t[0x9c + i] = "PixelShaderConstant" + n
    t[0xa4] = "Texture"
    samp = ["ADDRESSU", "ADDRESSV", "ADDRESSW", "BORDERCOLOR", "MAGFILTER",
            "MINFILTER", "MIPFILTER", "MIPMAPLODBIAS", "MAXMIPLEVEL",
            "MAXANISOTROPY", "SRGBTEXTURE", "ELEMENTINDEX", "DMAPOFFSET"]
    for i, n in enumerate(samp):
        t[0xa5 + i] = n
    return t


STATE_NAMES = _rs_table()
STAGE_STATES = {op for op, n in STATE_NAMES.items() if n.startswith("TSS_")} | \
    set(range(0x7e, 0x7f)) | set(range(0x84, 0x92)) | set(range(0xa4, 0xb2))

# Enumerated state values, for readability (D3DCULL, D3DCMPFUNC, D3DBLEND, ...)
_CMP = {1: "NEVER", 2: "LESS", 3: "EQUAL", 4: "LESSEQUAL", 5: "GREATER",
        6: "NOTEQUAL", 7: "GREATEREQUAL", 8: "ALWAYS"}
_BLEND = {1: "ZERO", 2: "ONE", 3: "SRCCOLOR", 4: "INVSRCCOLOR", 5: "SRCALPHA",
          6: "INVSRCALPHA", 7: "DESTALPHA", 8: "INVDESTALPHA", 9: "DESTCOLOR",
          10: "INVDESTCOLOR", 11: "SRCALPHASAT", 12: "BOTHSRCALPHA",
          13: "BOTHINVSRCALPHA", 14: "BLENDFACTOR", 15: "INVBLENDFACTOR"}
_TOP = {1: "DISABLE", 2: "SELECTARG1", 3: "SELECTARG2", 4: "MODULATE",
        5: "MODULATE2X", 6: "MODULATE4X", 7: "ADD", 8: "ADDSIGNED",
        9: "ADDSIGNED2X", 10: "SUBTRACT", 11: "ADDSMOOTH",
        12: "BLENDDIFFUSEALPHA", 13: "BLENDTEXTUREALPHA", 14: "BLENDFACTORALPHA",
        15: "BLENDTEXTUREALPHAPM", 16: "BLENDCURRENTALPHA", 17: "PREMODULATE",
        18: "MODULATEALPHA_ADDCOLOR", 19: "MODULATECOLOR_ADDALPHA",
        20: "MODULATEINVALPHA_ADDCOLOR", 21: "MODULATEINVCOLOR_ADDALPHA",
        22: "BUMPENVMAP", 23: "BUMPENVMAPLUMINANCE", 24: "DOTPRODUCT3",
        25: "MULTIPLYADD", 26: "LERP"}
_TA = {0: "DIFFUSE", 1: "CURRENT", 2: "TEXTURE", 3: "TFACTOR", 4: "SPECULAR",
       5: "TEMP", 6: "CONSTANT"}
_FILTER = {0: "NONE", 1: "POINT", 2: "LINEAR", 3: "ANISOTROPIC",
           6: "PYRAMIDALQUAD", 7: "GAUSSIANQUAD"}
_ADDR = {1: "WRAP", 2: "MIRROR", 3: "CLAMP", 4: "BORDER", 5: "MIRRORONCE"}
_MSRC = {0: "MATERIAL", 1: "COLOR1", 2: "COLOR2"}
STATE_ENUMS = {
    "CULLMODE": {1: "NONE", 2: "CW", 3: "CCW"},
    "ZFUNC": _CMP, "ALPHAFUNC": _CMP, "STENCILFUNC": _CMP,
    "SRCBLEND": _BLEND, "DESTBLEND": _BLEND, "SRCBLENDALPHA": _BLEND,
    "DESTBLENDALPHA": _BLEND,
    "BLENDOP": {1: "ADD", 2: "SUBTRACT", 3: "REVSUBTRACT", 4: "MIN", 5: "MAX"},
    "FILLMODE": {1: "POINT", 2: "WIREFRAME", 3: "SOLID"},
    "SHADEMODE": {1: "FLAT", 2: "GOURAUD", 3: "PHONG"},
    "TSS_COLOROP": _TOP, "TSS_ALPHAOP": _TOP,
    "MAGFILTER": _FILTER, "MINFILTER": _FILTER, "MIPFILTER": _FILTER,
    "ADDRESSU": _ADDR, "ADDRESSV": _ADDR, "ADDRESSW": _ADDR,
    "LIGHT_TYPE": {1: "POINT", 2: "SPOT", 3: "DIRECTIONAL"},
    "DIFFUSEMATERIALSOURCE": _MSRC, "SPECULARMATERIALSOURCE": _MSRC,
    "AMBIENTMATERIALSOURCE": _MSRC, "EMISSIVEMATERIALSOURCE": _MSRC,
}
for _n in ("COLORARG0", "COLORARG1", "COLORARG2", "ALPHAARG0", "ALPHAARG1",
           "ALPHAARG2", "RESULTARG"):
    STATE_ENUMS["TSS_" + _n] = _TA


def state_enum_note(sname, val):
    """' (D3D enum name)' for a constant integer state value, else ''."""
    base = sname.split("[")[0]
    if not (isinstance(val, list) and len(val) == 1 and isinstance(val[0], int)) \
            or isinstance(val[0], bool):
        return ""
    v = val[0]
    if base in ("COLORWRITEENABLE", "COLORWRITEENABLE1", "COLORWRITEENABLE2",
                "COLORWRITEENABLE3"):
        return " (%s)" % ("|".join(c for i, c in enumerate("RGBA") if v & (1 << i)) or "none")
    if base.startswith("TSS_") and base[4:] in ("COLORARG0", "COLORARG1", "COLORARG2",
                                                 "ALPHAARG0", "ALPHAARG1", "ALPHAARG2",
                                                 "RESULTARG"):
        n = _TA.get(v & 0xF, str(v & 0xF))
        if v & 0x10:
            n += "|COMPLEMENT"
        if v & 0x20:
            n += "|ALPHAREPLICATE"
        return " (%s)" % n
    if base == "TSS_TEXTURETRANSFORMFLAGS":
        n = {0: "DISABLE", 1: "COUNT1", 2: "COUNT2", 3: "COUNT3", 4: "COUNT4"}.get(v & 0xFF, str(v & 0xFF))
        if v & 256:
            n += "|PROJECTED"
        return " (%s)" % n
    e = STATE_ENUMS.get(base)
    if e and v in e:
        return " (%s)" % e[v]
    return ""


def fmt_float(f):
    if f != f or f in (float("inf"), float("-inf")):
        return repr(f)
    if f == int(f) and abs(f) < 1e9:
        return str(int(f))
    s = "%.9g" % f
    # prefer a shorter form if it round-trips through float32
    for p in range(1, 10):
        t = "%.*g" % (p, f)
        if struct.unpack("<f", struct.pack("<f", float(t)))[0] == \
                struct.unpack("<f", struct.pack("<f", f))[0]:
            return t
    return s


def u2f(u):
    return struct.unpack("<f", struct.pack("<I", u))[0]


# ---------------------------------------------------------------------------
# D3D9 shader disassembler

OPCODES = {
    0: ("nop", 0, 0), 1: ("mov", 1, 1), 2: ("add", 1, 2), 3: ("sub", 1, 2),
    4: ("mad", 1, 3), 5: ("mul", 1, 2), 6: ("rcp", 1, 1), 7: ("rsq", 1, 1),
    8: ("dp3", 1, 2), 9: ("dp4", 1, 2), 10: ("min", 1, 2), 11: ("max", 1, 2),
    12: ("slt", 1, 2), 13: ("sge", 1, 2), 14: ("exp", 1, 1), 15: ("log", 1, 1),
    16: ("lit", 1, 1), 17: ("dst", 1, 2), 18: ("lrp", 1, 3), 19: ("frc", 1, 1),
    20: ("m4x4", 1, 2), 21: ("m4x3", 1, 2), 22: ("m3x4", 1, 2),
    23: ("m3x3", 1, 2), 24: ("m3x2", 1, 2), 25: ("call", 0, 1),
    26: ("callnz", 0, 2), 27: ("loop", 0, 2), 28: ("ret", 0, 0),
    29: ("endloop", 0, 0), 30: ("label", 0, 1), 31: ("dcl", 1, 0),
    32: ("pow", 1, 2), 33: ("crs", 1, 2), 34: ("sgn", 1, 3), 35: ("abs", 1, 1),
    36: ("nrm", 1, 1), 37: ("sincos", 1, 3), 38: ("rep", 0, 1),
    39: ("endrep", 0, 0), 40: ("if", 0, 1), 41: ("ifc", 0, 2),
    42: ("else", 0, 0), 43: ("endif", 0, 0), 44: ("break", 0, 0),
    45: ("breakc", 0, 2), 46: ("mova", 1, 1), 47: ("defb", 1, 0),
    48: ("defi", 1, 0),
    64: ("texcoord", 1, 0), 65: ("texkill", 1, 0), 66: ("tex", 1, 0),
    67: ("texbem", 1, 1), 68: ("texbeml", 1, 1), 69: ("texreg2ar", 1, 1),
    70: ("texreg2gb", 1, 1), 71: ("texm3x2pad", 1, 1),
    72: ("texm3x2tex", 1, 1), 73: ("texm3x3pad", 1, 1),
    74: ("texm3x3tex", 1, 1), 76: ("texm3x3spec", 1, 2),
    77: ("texm3x3vspec", 1, 1), 78: ("expp", 1, 1), 79: ("logp", 1, 1),
    80: ("cnd", 1, 3), 81: ("def", 1, 0), 82: ("texreg2rgb", 1, 1),
    83: ("texdp3tex", 1, 1), 84: ("texm3x2depth", 1, 1), 85: ("texdp3", 1, 1),
    86: ("texm3x3", 1, 1), 87: ("texdepth", 1, 0), 88: ("cmp", 1, 3),
    89: ("bem", 1, 2), 90: ("dp2add", 1, 3), 91: ("dsx", 1, 1),
    92: ("dsy", 1, 1), 93: ("texldd", 1, 4), 94: ("setp", 1, 2),
    95: ("texldl", 1, 2), 96: ("breakp", 0, 1),
}
OP_PHASE, OP_COMMENT, OP_END = 0xFFFD, 0xFFFE, 0xFFFF

USAGES = ["position", "blendweight", "blendindices", "normal", "psize",
          "texcoord", "tangent", "binormal", "tessfactor", "positiont",
          "color", "fog", "depth", "sample"]
SAMPLER_TEX = {0: "", 1: "_1d", 2: "_2d", 3: "_cube", 4: "_volume"}
COMPARISONS = {1: "_gt", 2: "_eq", 3: "_ge", 4: "_lt", 5: "_ne", 6: "_le"}
SHIFTS = {1: "_x2", 2: "_x4", 3: "_x8", 13: "_d8", 14: "_d4", 15: "_d2"}
CTAB_SETS = {0: "b", 1: "i", 2: "c", 3: "s"}
XYZW = "xyzw"


class ShaderError(Exception):
    pass


def fourcc(v):
    return struct.pack("<I", v).decode("latin-1")


def parse_ctab(blob):
    """Parse a CTAB comment body (bytes after the FOURCC)."""
    def rd(fmt, off):
        return struct.unpack_from(fmt, blob, off)

    def cstr(off):
        end = blob.index(b"\0", off)
        return blob[off:end].decode("latin-1")

    size, creator, version, nconst, cinfo, flags, target = rd("<7I", 0)
    consts = []
    for i in range(nconst):
        name, rset, ridx, rcount, _res, tinfo, defval = rd("<IHHHHII", cinfo + 20 * i)
        cls, typ, rows, cols, elems, members, minfo = rd("<HHHHHHI", tinfo)
        consts.append(dict(name=cstr(name), set=rset, index=ridx, count=rcount,
                           cls=cls, type=typ, rows=rows, cols=cols,
                           elements=elems, members=members,
                           default=defval))
    return dict(creator=cstr(creator) if creator else "",
                target=cstr(target) if target else "",
                version=version, flags=flags, constants=consts)


def ctab_typename(c):
    t = PT_NAMES.get(c["type"], "type%d" % c["type"])
    if c["cls"] == PC_VECTOR:
        t += "%d" % c["cols"]
    elif c["cls"] in (PC_MATRIX_ROWS, PC_MATRIX_COLUMNS):
        t += "%dx%d" % (c["rows"], c["cols"])
    elif c["cls"] == PC_STRUCT:
        t = "struct"
    if c["elements"] > 1:
        t += "[%d]" % c["elements"]
    return t


def ctab_regname(c):
    p = CTAB_SETS.get(c["set"], "?")
    if c["count"] <= 1:
        return "%s%d" % (p, c["index"])
    return "%s%d..%s%d" % (p, c["index"], p, c["index"] + c["count"] - 1)


def ctab_regmap(ctab):
    """(regset prefix, index) -> 'Name' or 'Name[i]' for each register."""
    m = {}
    if not ctab:
        return m
    for c in ctab["constants"]:
        p = CTAB_SETS.get(c["set"], "?")
        for k in range(c["count"]):
            m[(p, c["index"] + k)] = c["name"] if c["count"] == 1 else "%s[%d]" % (c["name"], k)
    return m


class Shader:
    """Disassembles one D3D9 (or preshader 'FX') token stream."""

    def __init__(self, data, off=0):
        self.data = data
        self.start = off
        self.version = self.u32(off)
        hi = self.version >> 16
        if hi == 0xFFFE:
            self.kind = "vs"
        elif hi == 0xFFFF:
            self.kind = "ps"
        elif hi == 0x4658:
            self.kind = "fx"
        else:
            raise ShaderError("bad version token %#x" % self.version)
        self.major = (self.version >> 8) & 0xFF
        self.minor = self.version & 0xFF
        self.lines = []          # (text, comment) pairs
        self.ctab = None
        self.clit = []
        self.fxlc = []           # raw FXLC body dwords
        self.pres = None         # nested preshader
        self.comments = []
        self.unknown = []        # unknown opcodes encountered
        self.used_regs = []
        self._parse()

    def u32(self, off):
        if off + 4 > len(self.data):
            raise ShaderError("token stream runs past end of data")
        return struct.unpack_from("<I", self.data, off)[0]

    @property
    def profile(self):
        if self.kind == "fx":
            return "fx_%d_%d (preshader)" % (self.major, self.minor)
        mn = {0xFF: "sw"}.get(self.minor, str(self.minor))
        if self.kind == "ps" and self.major == 2 and self.minor == 1:
            mn = "x"
        if self.kind == "vs" and self.major == 2 and self.minor == 1:
            mn = "x"
        return "%s_%d_%s" % (self.kind, self.major, mn)

    # -- register formatting ------------------------------------------------
    def regname(self, rtype, num, is_dst):
        vs, ps = self.kind == "vs", self.kind == "ps"
        if rtype == 0:
            return "r%d" % num
        if rtype == 1:
            return "v%d" % num
        if rtype in (2, 11, 12, 13):
            return "c%d" % (num + {2: 0, 11: 2048, 12: 4096, 13: 6144}[rtype])
        if rtype == 3:
            return ("a%d" % num) if vs else ("t%d" % num)
        if rtype == 4:
            return {0: "oPos", 1: "oFog", 2: "oPts"}.get(num, "rastout%d" % num)
        if rtype == 5:
            return "oD%d" % num
        if rtype == 6:
            return ("o%d" % num) if (vs and self.major >= 3) else ("oT%d" % num)
        if rtype == 7:
            return "i%d" % num
        if rtype == 8:
            return "oC%d" % num
        if rtype == 9:
            return "oDepth"
        if rtype == 10:
            return "s%d" % num
        if rtype == 14:
            return "b%d" % num
        if rtype == 15:
            return "aL"
        if rtype == 16:
            return "half%d" % num
        if rtype == 17:
            return {0: "vPos", 1: "vFace"}.get(num, "misc%d" % num)
        if rtype == 18:
            return "l%d" % num
        if rtype == 19:
            return "p%d" % num
        return "reg%d_%d" % (rtype, num)

    @staticmethod
    def rtype(tok):
        return ((tok >> 28) & 7) | (((tok >> 11) & 3) << 3)

    def note_reg(self, rtype, num):
        if rtype in (2, 11, 12, 13):
            num += {2: 0, 11: 2048, 12: 4096, 13: 6144}[rtype]
            self.used_regs.append(("c", num))
        elif rtype == 7:
            self.used_regs.append(("i", num))
        elif rtype == 14:
            self.used_regs.append(("b", num))
        elif rtype == 10:
            self.used_regs.append(("s", num))

    def fmt_dst(self, tok):
        rt, num = self.rtype(tok), tok & 0x7FF
        self.note_reg(rt, num)
        s = self.regname(rt, num, True)
        mask = (tok >> 16) & 0xF
        if mask != 0xF:
            s += "." + "".join(XYZW[i] for i in range(4) if mask & (1 << i))
        return s

    def dst_mods(self, tok):
        mod = (tok >> 20) & 0xF
        shift = (tok >> 24) & 0xF
        s = SHIFTS.get(shift, "") if shift else ""
        if mod & 1:
            s += "_sat"
        if mod & 2:
            s += "_pp"
        if mod & 4:
            s += "_centroid"
        return s

    def swizzle(self, sw):
        comps = [XYZW[(sw >> (2 * i)) & 3] for i in range(4)]
        if sw == 0xE4:
            return ""
        if len(set(comps)) == 1:
            return "." + comps[0]
        return "." + "".join(comps)

    def fmt_src(self, tok, reltok=None):
        rt, num = self.rtype(tok), tok & 0x7FF
        self.note_reg(rt, num)
        base = self.regname(rt, num, False)
        if tok & 0x2000:  # relative addressing
            if reltok is None:  # vs_1_x: implicit a0.x
                rel = "a0.x"
            else:
                rrt, rnum = self.rtype(reltok), reltok & 0x7FF
                comp = XYZW[(reltok >> 16) & 3]
                rel = "%s.%s" % (self.regname(rrt, rnum, False), comp)
            prefix = base.rstrip("0123456789")
            base = "%s[%s + %d]" % (prefix, rel, num) if num else "%s[%s]" % (prefix, rel)
            if rt in (2, 11, 12, 13):
                # relative constant: note base register only
                pass
        sw = self.swizzle((tok >> 16) & 0xFF)
        mod = (tok >> 24) & 0xF
        r = base + sw
        if mod == 0:
            return r
        if mod == 1:
            return "-" + r
        if mod == 2:
            return base + "_bias" + sw
        if mod == 3:
            return "-" + base + "_bias" + sw
        if mod == 4:
            return base + "_bx2" + sw
        if mod == 5:
            return "-" + base + "_bx2" + sw
        if mod == 6:
            return "1-" + r
        if mod == 7:
            return base + "_x2" + sw
        if mod == 8:
            return "-" + base + "_x2" + sw
        if mod == 9:
            return base + "_dz" + sw
        if mod == 10:
            return base + "_dw" + sw
        if mod == 11:
            return "%s_abs%s" % (base, sw)
        if mod == 12:
            return "-%s_abs%s" % (base, sw)
        if mod == 13:
            return "!" + r
        return r + "_srcmod%d" % mod

    # -- main loop -----------------------------------------------------------
    def emit(self, text, comment=""):
        self.lines.append((text, comment))

    def _parse(self):
        p = self.start + 4
        self.emit(self.profile if self.kind != "fx" else "fx_%d_%d" % (self.major, self.minor))
        sm2 = self.major >= 2
        while True:
            tok = self.u32(p)
            op = tok & 0xFFFF
            if op == OP_END:
                p += 4
                break
            if op == OP_COMMENT:
                n = (tok >> 16) & 0x7FFF
                body = self.data[p + 4:p + 4 + 4 * n]
                self._comment(body)
                p += 4 + 4 * n
                continue
            if op == OP_PHASE:
                self.emit("phase")
                p += 4
                continue
            info = OPCODES.get(op)
            if info is None:
                self.unknown.append(op)
                self.emit("; unknown opcode %#x (token %#010x)" % (op, tok))
                if sm2:
                    p += 4 + 4 * ((tok >> 24) & 0xF)
                    continue
                raise ShaderError("unknown opcode %#x in %s at %#x" % (op, self.profile, p))
            name, ndst, nsrc = info
            if sm2:
                length = (tok >> 24) & 0xF
            else:
                length = self._len_sm1(op, ndst, nsrc)
            params = [self.u32(p + 4 + 4 * i) for i in range(length)]
            p += 4 + 4 * length
            self.used_regs = []
            text = self._format(op, name, tok, params, ndst, nsrc)
            regs = []
            seen = set()
            for r in self.used_regs:
                if r not in seen:
                    seen.add(r)
                    regs.append(r)
            self.emit(text, regs)
        self.end = p
        self.size = p - self.start

    def _len_sm1(self, op, ndst, nsrc):
        if op == 31:
            return 2
        if op == 81:
            return 5
        if op in (47,):
            return 2
        if op == 48:
            return 5
        if self.kind == "ps" and op == 66 and (self.major, self.minor) >= (1, 4):
            return 2  # texld r, t
        if self.kind == "ps" and op == 64 and (self.major, self.minor) >= (1, 4):
            return 2  # texcrd r, t
        if op == 37:
            return 4
        # vs_1_x relative addressing uses no extra token
        return ndst + nsrc

    def _format(self, op, name, tok, params, ndst, nsrc):
        prefix = "+" if (tok & 0x40000000) and self.kind == "ps" and self.major < 2 else ""
        if op == 31:  # dcl
            d, reg = params[0], params[1]
            rt = self.rtype(reg)
            if rt == 10:  # sampler
                ttype = (d >> 27) & 0xF
                return "dcl%s %s" % (SAMPLER_TEX.get(ttype, "_tex%d" % ttype),
                                     self.fmt_dst(reg) + self.dst_mods(reg))
            if self.kind == "vs" or (self.kind == "ps" and self.major >= 3 and rt == 1):
                usage, uidx = d & 0x1F, (d >> 16) & 0xF
                us = USAGES[usage] if usage < len(USAGES) else "usage%d" % usage
                if uidx:
                    us += str(uidx)
                return "dcl_%s%s %s" % (us, self.dst_mods(reg), self.fmt_dst(reg))
            return "dcl%s %s" % (self.dst_mods(reg), self.fmt_dst(reg))
        if op == 81:  # def
            return "def %s, %s" % (self.fmt_dst(params[0]),
                                   ", ".join(fmt_float(u2f(x)) for x in params[1:5]))
        if op == 48:
            return "defi %s, %s" % (self.fmt_dst(params[0]),
                                    ", ".join(str(struct.unpack("<i", struct.pack("<I", x))[0])
                                              for x in params[1:5]))
        if op == 47:
            return "defb %s, %s" % (self.fmt_dst(params[0]),
                                    "true" if params[1] else "false")
        opname = name
        ctrl = (tok >> 16) & 0xFF
        if op in (41, 45, 94):
            opname += COMPARISONS.get(ctrl & 7, "")
        if self.kind == "ps":
            if op == 66:
                if self.major >= 2:
                    opname = {0: "texld", 1: "texldp", 2: "texldb"}.get(ctrl & 3, "texld")
                elif (self.major, self.minor) >= (1, 4):
                    opname = "texld"
            if op == 64 and (self.major, self.minor) >= (1, 4):
                opname = "texcrd"
        # split params into dst / src honouring relative-address tokens (SM2+)
        i = 0
        dst = None
        mods = ""
        pred = None
        if ndst:
            dst = params[i]
            i += 1
            mods = self.dst_mods(dst)
            if self.major >= 2 and dst & 0x2000:
                i += 1  # dst relative address token (vs_3_0 o#); ignore
        if tok & 0x10000000 and self.major >= 2:  # predicated
            pred = params[i]
            i += 1
        srcs = []
        while i < len(params):
            s = params[i]
            i += 1
            rel = None
            if self.major >= 2 and s & 0x2000:
                rel = params[i]
                i += 1
            srcs.append(self.fmt_src(s, rel))
        ops = []
        if dst is not None:
            ops.append(self.fmt_dst(dst))
        ops += srcs
        text = prefix + opname + mods
        if pred is not None:
            text = "(%s) %s" % (self.fmt_src(pred), text)
        if ops:
            text += " " + ", ".join(ops)
        return text

    def _comment(self, body):
        if len(body) < 4:
            self.comments.append(("", len(body)))
            return
        cc = fourcc(struct.unpack_from("<I", body, 0)[0])
        self.comments.append((cc, len(body)))
        rest = body[4:]
        if cc == "CTAB":
            try:
                self.ctab = parse_ctab(rest)
            except (struct.error, ValueError) as e:
                self.emit("; CTAB parse error: %s" % e)
        elif cc == "CLIT":
            n = struct.unpack_from("<I", rest, 0)[0]
            self.clit = list(struct.unpack_from("<%dd" % n, rest, 4))
        elif cc == "FXLC":
            self.fxlc = list(struct.unpack_from("<%dI" % (len(rest) // 4), rest, 0))
        elif cc == "PRES":
            try:
                self.pres = Shader(rest, 0)
            except ShaderError as e:
                self.emit("; PRES parse error: %s" % e)
        elif cc == "PRSI":
            self.prsi = list(struct.unpack_from("<%dI" % (len(rest) // 4), rest, 0))

    # -- output ----------------------------------------------------------------
    def ctab_lines(self, indent="// "):
        out = []
        if not self.ctab:
            return out
        if self.ctab["creator"]:
            out.append(indent + " Compiled by: %s, target %s" % (self.ctab["creator"], self.ctab["target"]))
        if not self.ctab["constants"]:
            out.append(indent + "(no constants)")
        rows = sorted(self.ctab["constants"], key=lambda c: (c["set"], c["index"]))
        for c in rows:
            out.append(indent + "  %-10s = %-24s (%s)" % (ctab_regname(c), c["name"], ctab_typename(c)))
        return out

    def listing(self, indent="    "):
        out = []
        regmap = ctab_regmap(self.ctab)
        if self.ctab:
            out.append(indent + "// Constant table:")
            out += [indent + l for l in self.ctab_lines("//")]
        if self.kind == "fx":
            out += [indent + l for l in self.fxlc_lines()]
            return out
        if self.pres:
            out.append(indent + "// Preshader (runs on CPU, writes constants before draw):")
            out += self.pres.listing(indent + "//   ")
        width = max([len(t) for t, c in self.lines] + [0])
        width = min(max(width, 24), 44)
        for text, regs in self.lines:
            cm = ""
            if regs:
                names = []
                for r in regs:
                    if r in regmap:
                        names.append("%s%d=%s" % (r[0], r[1], regmap[r]))
                if names:
                    cm = "  // " + ", ".join(names)
            out.append(indent + (text.ljust(width) + cm if cm else text))
        return out

    # -- preshader (FXLC) ------------------------------------------------------
    PRES_OPS = {
        0x100: "mov", 0x101: "neg", 0x103: "rcp", 0x104: "frc", 0x105: "exp",
        0x106: "log", 0x107: "rsq", 0x108: "sin", 0x109: "cos", 0x10a: "asin",
        0x10b: "acos", 0x10c: "atan", 0x200: "min", 0x201: "max", 0x202: "lt",
        0x203: "ge", 0x204: "add", 0x205: "mul", 0x206: "atan2", 0x208: "div",
        0x300: "cmp", 0x301: "movc", 0x500: "dot", 0x502: "dotswiz6",
        0x503: "dotswiz8", 0xa00: "min", 0xa01: "max", 0xa02: "lt", 0xa03: "ge",
        0xa04: "add", 0xa05: "mul", 0xa06: "atan2", 0xa08: "div",
    }
    PRES_TABLES = {1: "lit", 2: "c", 4: "out", 7: "r"}

    def _pres_operand(self, table, offset, ncomp, regmap):
        tname = self.PRES_TABLES.get(table, "tab%d" % table)
        if tname == "lit":
            vals = [self.clit[offset + k] if offset + k < len(self.clit) else float("nan")
                    for k in range(ncomp)]
            s = ", ".join(fmt_float(v) for v in vals)
            return s if ncomp == 1 else "(%s)" % s
        reg, comp = divmod(offset, 4)
        sw = "".join(XYZW[(comp + k) % 4] for k in range(ncomp))
        s = "%s%d.%s" % (tname, reg, sw)
        if tname == "c" and ("c", reg) in regmap:
            s += "{%s}" % regmap[("c", reg)]
        return s

    def fxlc_lines(self):
        out = []
        if not self.fxlc:
            return out
        regmap = ctab_regmap(self.ctab)
        d = self.fxlc
        n = d[0]
        p = 1
        out.append("// Expression (%d instruction%s):" % (n, "" if n == 1 else "s"))
        for _ in range(n):
            if p + 2 > len(d):
                out.append("//   <truncated>")
                break
            code, nin = d[p], d[p + 1]
            p += 2
            op = (code >> 20) & 0x7FF
            ncomp = code & 0xFFFF
            name = self.PRES_OPS.get(op)
            if name is None:
                self.unknown.append(("fxlc", op))
                name = "op%#x" % op
            args = []
            for a in range(nin + 1):
                rel = d[p]
                if rel:
                    itab, ioff = d[p + 1], d[p + 2]
                    p += 3
                else:
                    itab = ioff = None
                    p += 1
                table, offset = d[p], d[p + 1]
                p += 2
                cnt = 1 if (op >= 0xa00 and a == 0 and a < nin) else ncomp
                if op in (0x500,) and a < nin:
                    cnt = ncomp
                s = self._pres_operand(table, offset, cnt, regmap)
                if itab is not None:
                    s = "%s[%s]" % (s, self._pres_operand(itab, ioff, 1, regmap))
                args.append(s)
            out.append("//   %-5s %s" % (name, ", ".join([args[-1]] + args[:-1])))
        return out


def is_shader_version(v):
    hi, maj = v >> 16, (v >> 8) & 0xFF
    return (hi in (0xFFFE, 0xFFFF) and 1 <= maj <= 3) or (hi == 0x4658 and maj in (1, 2))


def try_shader(data, off=0):
    if len(data) - off < 8:
        return None
    if not is_shader_version(struct.unpack_from("<I", data, off)[0]):
        return None
    try:
        return Shader(data, off)
    except (ShaderError, struct.error, IndexError):
        return None


# ---------------------------------------------------------------------------
# fx_2_0 effect parser

class EffectError(Exception):
    pass


class Effect:
    def __init__(self, data):
        self.data = data
        tag, blob = struct.unpack_from("<II", data, 0)
        if tag != FX_TAG:
            raise EffectError("not an fx_2_0 effect (tag %#x)" % tag)
        self.base = 8
        self.blob_size = blob
        self.objects = {}      # id -> dict(type, owner, data, data_off)
        self.shaders = []
        self.expressions = []
        p = self.base + blob
        (self.nparams, self.ntech, self.unk, self.nobjects) = struct.unpack_from("<4I", data, p)
        p += 16
        self.params = []
        for i in range(self.nparams):
            prm, p = self._param(p, "parameter %d" % i)
            self.params.append(prm)
        self.techniques = []
        for t in range(self.ntech):
            name_off, nanno, npass = struct.unpack_from("<3I", data, p)
            p += 12
            tech = dict(name=self.name(name_off), annos=[], passes=[])
            for a in range(nanno):
                anno, p = self._anno(p, "technique %s annotation" % tech["name"])
                tech["annos"].append(anno)
            for ps in range(npass):
                name_off, nanno, nstate = struct.unpack_from("<3I", data, p)
                p += 12
                pas = dict(name=self.name(name_off) or "p%d" % ps, annos=[], states=[])
                for a in range(nanno):
                    anno, p = self._anno(p, "pass annotation")
                    pas["annos"].append(anno)
                for s in range(nstate):
                    st, p = self._state(p, "technique #%d '%s'%s pass '%s'" % (
                        t, tech["name"], self._anno_tag(tech["annos"]), pas["name"]))
                    pas["states"].append(st)
                tech["passes"].append(pas)
            self.techniques.append(tech)
        nstrings, nres = struct.unpack_from("<2I", data, p)
        p += 8
        for i in range(nstrings):
            oid, size = struct.unpack_from("<2I", data, p)
            p += 8
            obj = self.objects.setdefault(oid, dict(type=None, owner="?"))
            obj["data"] = data[p:p + size]
            obj["data_off"] = p
            p += (size + 3) & ~3
        self.resources = []
        for i in range(nres):
            tech, index, elem, sidx, usage, size = struct.unpack_from("<6I", data, p)
            p += 24
            blob_off = p
            blob_data = data[p:p + size]
            p += (size + 3) & ~3
            if tech == 0xFFFFFFFF:
                prm = self.params[index]
                tgt = prm
                if elem != 0xFFFFFFFF and prm["elements"]:
                    tgt = dict(prm, name="%s[%d]" % (prm["name"], elem),
                               value=prm["value"][elem])
                state = tgt["value"][sidx]
                where = "parameter '%s' state %d" % (tgt["name"], sidx)
            else:
                state = self.techniques[tech]["passes"][index]["states"][sidx]
                where = "technique '%s' pass '%s'" % (self.techniques[tech]["name"],
                                                      self.techniques[tech]["passes"][index]["name"])
            state["usage"] = usage
            state["res_off"] = blob_off
            state["res"] = blob_data
            self.resources.append(state)
        self.end = p
        self._collect(where_unused=None)

    def _anno_tag(self, annos):
        """Short ' [k=v, ...]' tag from scalar annotations (to tell apart
        techniques that share a name)."""
        items = []
        for a in annos:
            v = a["value"]
            if isinstance(v, list) and len(v) == 1:
                items.append("%s=%s" % (a["name"], fmt_float(v[0]) if isinstance(v[0], float) else v[0]))
        return " [%s]" % ", ".join(items) if items else ""

    # -- low level -------------------------------------------------------------
    def u32(self, off):
        return struct.unpack_from("<I", self.data, off)[0]

    def name(self, off):
        """Length-prefixed string at blob offset."""
        p = self.base + off
        n = self.u32(p)
        return self.data[p + 4:p + 4 + n].split(b"\0")[0].decode("latin-1")

    def _typedef(self, off):
        """Parse a typedef at absolute offset; returns (td, next_offset)."""
        typ, cls, name, sem, elems = struct.unpack_from("<5I", self.data, off)
        p = off + 20
        td = dict(type=typ, cls=cls, name=self.name(name), semantic=self.name(sem) if sem else "",
                  elements=elems, rows=1, cols=1, members=[])
        if cls in (PC_SCALAR, PC_VECTOR, PC_MATRIX_ROWS, PC_MATRIX_COLUMNS):
            td["cols"], td["rows"] = struct.unpack_from("<2I", self.data, p)
            p += 8
        elif cls == PC_STRUCT:
            nmem = self.u32(p)
            p += 4
            for _ in range(nmem):
                m, p = self._typedef(p)
                td["members"].append(m)
        return td, p

    def _value(self, td, off, owner, elem=True):
        """Parse a value laid out per typedef; returns (value, next_offset)."""
        if elem and td["elements"]:
            vals = []
            for i in range(td["elements"]):
                v, off = self._value(td, off, "%s[%d]" % (owner, i), elem=False)
                vals.append(v)
            return vals, off
        if td["cls"] == PC_STRUCT:
            vals = []
            for m in td["members"]:
                v, off = self._value(m, off, "%s.%s" % (owner, m["name"]))
                vals.append(v)
            return vals, off
        if td["cls"] == PC_OBJECT:
            if td["type"] in SAMPLER_TYPES:
                n = self.u32(off)
                off += 4
                states = []
                for _ in range(n):
                    st, off = self._state(off, "sampler " + owner, in_sampler=True)
                    states.append(st)
                return states, off
            oid = self.u32(off)
            obj = self.objects.setdefault(oid, dict(type=td["type"], owner=owner))
            obj["type"] = td["type"]
            obj["owner"] = owner
            return ("object", oid), off + 4
        n = td["rows"] * td["cols"]
        raw = struct.unpack_from("<%dI" % n, self.data, off)
        if td["type"] == 3:
            vals = [u2f(x) for x in raw]
        elif td["type"] == 1:
            vals = [bool(x) for x in raw]
        else:
            vals = [struct.unpack("<i", struct.pack("<I", x))[0] for x in raw]
        return vals, off + 4 * n

    def _param(self, p, owner):
        toff, voff, flags, nanno = struct.unpack_from("<4I", self.data, p)
        p += 16
        td, _ = self._typedef(self.base + toff)
        prm = dict(td, flags=flags, annos=[])
        prm["value"], _ = self._value(td, self.base + voff, td["name"])
        for _ in range(nanno):
            a, p = self._anno(p, td["name"])
            prm["annos"].append(a)
        return prm, p

    def _anno(self, p, owner):
        toff, voff = struct.unpack_from("<2I", self.data, p)
        td, _ = self._typedef(self.base + toff)
        val, _ = self._value(td, self.base + voff, "%s annotation %s" % (owner, td["name"]))
        return dict(td, value=val), p + 8

    def _state(self, p, owner, in_sampler=False):
        op, index, toff, voff = struct.unpack_from("<4I", self.data, p)
        td, _ = self._typedef(self.base + toff)
        sname = STATE_NAMES.get(op, "state_%#x" % op)
        if not in_sampler and (op in STAGE_STATES or index):
            sname += "[%d]" % index
        val, _ = self._value(td, self.base + voff, "%s %s" % (owner, sname))
        return dict(op=op, index=index, td=td, value=val, name=sname,
                    owner=owner, usage=None, res=None), p + 16

    # -- shader collection --------------------------------------------------------
    def _collect(self, where_unused):
        """Build the list of shaders / expressions with where they're used."""
        refs = {}
        for t in self.techniques:
            for pas in t["passes"]:
                for st in pas["states"]:
                    if st["td"]["type"] in (PT_PIXELSHADER, PT_VERTEXSHADER) and \
                            isinstance(st["value"], tuple):
                        refs.setdefault(st["value"][1], []).append("%s %s" % (st["owner"], st["name"]))
        self.shader_refs = refs
        # shaders stored as objects (inline in pass states or in parameters)
        for oid in sorted(self.objects):
            obj = self.objects[oid]
            dat = obj.get("data")
            if obj["type"] in (PT_PIXELSHADER, PT_VERTEXSHADER) and dat:
                sh = try_shader(dat)
                used = refs.get(oid, []) or ["%s (object %d)" % (obj["owner"], oid)]
                self.shaders.append(dict(shader=sh, off=obj["data_off"], size=len(dat),
                                         used=used, oid=oid))
        for st in self.resources:
            dat = st["res"]
            if st["usage"] == 0 and dat and st["td"]["type"] in (PT_PIXELSHADER, PT_VERTEXSHADER):
                sh = try_shader(dat)
                self.shaders.append(dict(shader=sh, off=st["res_off"], size=len(dat),
                                         used=["%s %s" % (st["owner"], st["name"])], oid=None))
                st["shader_index"] = None
        self.shaders.sort(key=lambda s: s["off"])
        for i, s in enumerate(self.shaders):
            s["index"] = i
        by_off = {s["off"]: s["index"] for s in self.shaders}
        self.shader_by_off = by_off
        self.shader_by_oid = {s["oid"]: s["index"] for s in self.shaders if s["oid"] is not None}

    # -- formatting -------------------------------------------------------------
    def fmt_value(self, td, val):
        if isinstance(val, tuple) and val and val[0] == "object":
            oid = val[1]
            if oid in self.shader_by_oid:
                return "shader #%d" % self.shader_by_oid[oid]
            obj = self.objects.get(oid, {})
            dat = obj.get("data")
            if td["type"] == 4 and dat is not None:
                return '"%s"' % dat.split(b"\0")[0].decode("latin-1")
            if dat:
                return "<object %d, %d bytes>" % (oid, len(dat))
            return "<object %d>" % oid
        if td["cls"] == PC_OBJECT and td["type"] in SAMPLER_TYPES and isinstance(val, list) \
                and not td["elements"]:
            return "sampler_state { %s }" % "; ".join(
                "%s = %s" % (st["name"], self.fmt_state_value(st)) for st in val)
        if td["elements"] and isinstance(val, list):
            sub = dict(td, elements=0)
            return "{ %s }" % ", ".join(self.fmt_value(sub, v) for v in val)
        if td["cls"] == PC_STRUCT:
            return "{ %s }" % ", ".join(self.fmt_value(m, v) for m, v in zip(td["members"], val))
        if isinstance(val, list):
            items = [fmt_float(v) if isinstance(v, float) else str(v).lower() for v in val]
            return items[0] if len(items) == 1 else "(%s)" % ", ".join(items)
        return repr(val)

    def fmt_state_value(self, st):
        if st.get("usage") == 1 and st.get("res") is not None:
            return "<%s>" % st["res"].split(b"\0")[0].decode("latin-1")
        if st.get("usage") == 2 and st.get("res") is not None:
            r = st["res"]
            n = struct.unpack_from("<I", r, 0)[0]
            pname = r[4:4 + n].split(b"\0")[0].decode("latin-1")
            ex = try_shader(r, 4 + ((n + 3) & ~3))
            self.expressions.append(ex)
            return "%s[expression]%s" % (pname, self._expr_inline(ex))
        if st.get("usage") == 0 and st.get("res") is not None:
            if st["td"]["type"] in (PT_PIXELSHADER, PT_VERTEXSHADER):
                idx = self.shader_by_off.get(st["res_off"])
                return "shader #%s" % idx
            ex = try_shader(st["res"])
            self.expressions.append(ex)
            return "expression%s" % self._expr_inline(ex)
        return self.fmt_value(st["td"], st["value"]) + state_enum_note(st["name"], st["value"])

    def _expr_inline(self, ex):
        if ex is None:
            return " <undecodable>"
        lines = ex.fxlc_lines()[1:]
        ins = [l[4:].strip() for l in lines]
        return " { %s }" % "; ".join(ins)

    def type_str(self, td):
        t = PT_NAMES.get(td["type"], "type%d" % td["type"])
        if td["cls"] == PC_VECTOR:
            t += str(td["cols"])
        elif td["cls"] in (PC_MATRIX_ROWS, PC_MATRIX_COLUMNS):
            t += "%dx%d" % (td["rows"], td["cols"])
        elif td["cls"] == PC_STRUCT:
            t = "struct { %s }" % " ".join("%s %s;" % (self.type_str(m), m["name"]) for m in td["members"])
        return t

    def decl_str(self, td):
        s = "%s %s" % (self.type_str(td), td["name"])
        if td["elements"]:
            s += "[%d]" % td["elements"]
        if td["semantic"]:
            s += " : " + td["semantic"]
        return s

    def annos_str(self, annos):
        if not annos:
            return ""
        return " < %s >" % " ".join("%s = %s;" % (self.decl_str(a), self.fmt_value(a, a["value"]))
                                     for a in annos)


# ---------------------------------------------------------------------------
# scanning fallback

def scan_shaders(data):
    """Find D3D9 shader token streams anywhere in a blob.

    A stream is accepted when it starts at a 4-byte aligned version token,
    the dword before it is a plausible byte size, and it disassembles to END.
    """
    found = []
    for off in range(4, len(data) - 8, 4):
        v = struct.unpack_from("<I", data, off)[0]
        hi, maj = v >> 16, (v >> 8) & 0xFF
        if hi not in (0xFFFE, 0xFFFF) or not (1 <= maj <= 3):
            continue
        size = struct.unpack_from("<I", data, off - 4)[0]
        if size < 8 or size > len(data) - off or size & 3:
            continue
        sh = try_shader(data, off)
        if sh and sh.size == size:
            found.append((off, sh))
    return found


# ---------------------------------------------------------------------------

def dump(path):
    data = open(path, "rb").read()
    out = []
    w = out.append
    name = os.path.basename(path)
    w("=" * 78)
    w("Effect %s  (%d bytes)" % (name, len(data)))
    w("=" * 78)
    stats = dict(shaders=0, profiles={}, unknown=[], problems=[])
    try:
        fx = Effect(data)
    except (EffectError, struct.error, IndexError, KeyError) as e:
        fx = None
        stats["problems"].append("effect structure parse failed: %r; falling back to scan" % (e,))
        w("!! effect structure parse failed (%r); listing shaders found by scanning" % (e,))

    scanned = scan_shaders(data)
    if fx:
        # render pass state text first (also decodes expressions)
        tech_text = []
        for t in fx.techniques:
            tech_text.append("technique #%d %s%s" % (fx.techniques.index(t), t["name"], fx.annos_str(t["annos"])))
            for pas in t["passes"]:
                tech_text.append("    pass %s%s" % (pas["name"], fx.annos_str(pas["annos"])))
                for st in pas["states"]:
                    tech_text.append("        %-22s = %s" % (st["name"], fx.fmt_state_value(st)))
        param_text = []
        for i, prm in enumerate(fx.params):
            val = fx.fmt_value(prm, prm["value"])
            param_text.append("  [%2d] %s%s = %s" % (i, fx.decl_str(prm), fx.annos_str(prm["annos"]), val))

        w("%d parameters, %d techniques, %d objects, %d shaders" %
          (fx.nparams, fx.ntech, fx.nobjects, len(fx.shaders)))
        w("")
        w("Names in file order: " + ", ".join(
            [p["name"] for p in fx.params] + ["technique " + t["name"] for t in fx.techniques]))
        w("")
        w("-- Parameters " + "-" * 64)
        out.extend(param_text)
        w("")
        w("-- Techniques " + "-" * 64)
        out.extend(tech_text)
        w("")
        for ex in fx.expressions:
            if ex is None:
                stats["problems"].append("undecodable state expression")
            else:
                stats["unknown"] += ex.unknown
        shaders = [(s["off"], s["shader"], s["used"], s["size"]) for s in fx.shaders]
        known = {s["off"] for s in fx.shaders}
        for off, sh in scanned:
            if off not in known:
                shaders.append((off, sh, ["(found by scan only; not referenced by effect tables)"], sh.size))
                stats["problems"].append("stream at %#x found by scan only" % off)
        missing = known - {o for o, _ in scanned}
        for off in sorted(missing):
            stats["problems"].append("structured shader at %#x not found by scan" % off)
    else:
        shaders = [(off, sh, ["?"], sh.size) for off, sh in scanned]

    w("-- Shaders " + "-" * 67)
    for i, (off, sh, used, size) in enumerate(shaders):
        w("")
        if sh is None:
            w("shader #%d @ file offset %#x, %d bytes: FAILED TO DECODE" % (i, off, size))
            stats["problems"].append("shader #%d at %#x failed to decode" % (i, off))
            continue
        stats["shaders"] += 1
        stats["profiles"][sh.profile] = stats["profiles"].get(sh.profile, 0) + 1
        stats["unknown"] += sh.unknown
        if sh.size != size:
            stats["problems"].append("shader #%d: END at %d bytes but blob is %d" % (i, sh.size, size))
        w("shader #%d: %s @ file offset %#x, %d bytes" % (i, sh.profile, off, size))
        for u in used:
            w("    used by: %s" % u)
        others = [c for c, n in sh.comments if c not in ("CTAB",)]
        if others:
            w("    // other comment blocks: %s" % ", ".join(others))
        out.extend(sh.listing("    "))
    w("")
    return "\n".join(out), stats


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("files", nargs="+", help="fx_2_0 effect binaries")
    ap.add_argument("--out", help="write <name>.txt per input into this directory")
    ap.add_argument("--summary", action="store_true", help="print a per-file summary to stderr")
    args = ap.parse_args(argv)
    if args.out:
        os.makedirs(args.out, exist_ok=True)
    rc = 0
    for f in args.files:
        text, stats = dump(f)
        if args.out:
            dst = os.path.join(args.out, os.path.splitext(os.path.basename(f))[0] + ".txt")
            with open(dst, "w") as fh:
                fh.write(text + "\n")
        else:
            sys.stdout.write(text + "\n")
        if args.summary or stats["unknown"] or stats["problems"]:
            prof = ", ".join("%s x%d" % kv for kv in sorted(stats["profiles"].items()))
            sys.stderr.write("%s: %d shaders (%s)%s%s\n" % (
                os.path.basename(f), stats["shaders"], prof,
                "; unknown opcodes: %s" % stats["unknown"] if stats["unknown"] else "",
                "; problems: %s" % "; ".join(stats["problems"]) if stats["problems"] else ""))
            if stats["unknown"] or stats["problems"]:
                rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
