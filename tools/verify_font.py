#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""验证生成的 ui_font_wqy12_cn.c: 用 C 规则解析字符串，检查表结构。"""
import re
import sys

def c_str_to_bytes(s):
    out = bytearray()
    i = 0
    n = len(s)
    while i < n:
        c = s[i]
        if c == '\\' and i + 1 < n and s[i+1] in '01234567':
            j = i + 1
            val = 0
            cnt = 0
            while j < n and cnt < 3 and s[j] in '01234567':
                val = val * 8 + int(s[j])
                j += 1
                cnt += 1
            out.append(val & 0xFF)
            i = j
        else:
            out.append(ord(c))
            i += 1
    return bytes(out)

path = r'c:\Users\usr12\Projects\fuckerdetector\main\src\ui_font_wqy12_cn.c'
text = open(path, encoding='utf-8').read()
m = re.search(r'=\s*((?:"(?:[^"\\]|\\.)*"\s*)+);', text, re.DOTALL)
assert m, '未找到字体数据'
literals = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
fb = bytearray()
for lit in literals:
    fb += c_str_to_bytes(lit)
print(f'解析出 {len(fb)} 字节 (预期 2608)')

def gw(off):
    return (fb[off] << 8) | fb[off+1]

spu = gw(21)
print(f'start_pos_unicode = {spu}')
table_start = 23 + spu
off0 = gw(table_start)
n = off0 // 4
print(f'表条目数 = {n} (off0={off0})')
print('前 6 条 (offset, enc):')
for i in range(min(6, n)):
    pos = table_start + i * 4
    print(f'  #{i}: offset={gw(pos)} enc=U+{gw(pos+2):04X} ({chr(gw(pos+2)) if 0x4E00 <= gw(pos+2) <= 0x9FFF else "?"})')
print('最后 2 条:')
for i in range(n-2, n):
    pos = table_start + i * 4
    print(f'  #{i}: offset={gw(pos)} enc=U+{gw(pos+2):04X}')

# glyph 区起点
gs = table_start + off0
print(f'\nglyph 数据区起点 = {gs}')

# 走 glyph 区
p = gs
cnt = 0
encs = []
while p + 3 < len(fb):
    enc = gw(p)
    size = fb[p+2]
    if enc == 0:
        print(f'  终止符 @{p}: enc=0 size=0 ✓')
        break
    encs.append(enc)
    cnt += 1
    p += size
print(f'glyph 条目: {cnt} 条, 结束于 {p}, 文件总长 {len(fb)}')
print(f'终止符后剩余 {len(fb)-p} 字节')

# 模拟 u8g2 查找"置" (0x7F6E) —— 之前卡死的字符
def lookup(encoding):
    p = 23
    if encoding <= 255:
        if encoding >= ord('a'):
            p += gw(19)
        elif encoding >= ord('A'):
            p += gw(17)
        while True:
            if p+1 >= len(fb): return None
            if fb[p+1] == 0: return None
            if fb[p] == encoding: return p+2
            p += fb[p+1]
    else:
        ts = p + gw(21)
        p = ts
        lut = ts
        while True:
            off = gw(lut); e = gw(lut+2)
            p += off
            lut += 4
            if not (e < encoding): break
        while True:
            if p+2 >= len(fb): return None
            e = gw(p)
            if e == 0: return None
            if e == encoding: return p+3
            p += fb[p+2]
            if p >= len(fb): return None

print(f'\n查找"置"(0x7F6E): {"成功 @ " + str(lookup(0x7F6E)) if lookup(0x7F6E) else "失败!"}')
print(f'查找"设"(0x8BBE): {"成功 @ " + str(lookup(0x8BBE)) if lookup(0x8BBE) else "失败!"}')
print(f'查找"一"(0x4E00,不在字体): {"找到(错误!)" if lookup(0x4E00) else "返回 NULL ✓"}')
print(f'查找"爱"(0x7231,不在字体): {"找到(错误!)" if lookup(0x7231) else "返回 NULL ✓"}')
