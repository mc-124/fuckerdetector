#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
从 u8g2_font_wqy12_t_gb2312 提取 UI 所需的汉字字形，生成自定义子集字体 C 文件。

生成的字体包含：
- 完整 ASCII 区（32-126 + gb2312 原有的扩展字符）
- UI 用到的汉字（39 个）

输出: main/src/ui_font_wqy12_cn.c
用法: python tools/gen_font_subset.py
"""
import re
import sys

FONT_SRC = r'c:\Users\usr12\Projects\fuckerdetector\managed_components\nixy4__u8g2\src\core\u8g2_fonts.c'
OUT_FILE = r'c:\Users\usr12\Projects\fuckerdetector\main\src\ui_font_wqy12_cn.c'
FONT_NAME = 'ui_font_wqy12_cn'


def parse_octal_string(s):
    """把 C 字符串字面量（八进制转义）解析为 bytes。"""
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


def extract_font_bytes(font_name):
    """从 u8g2_fonts.c 提取指定字体的原始字节（去掉 C 字符串转义）。"""
    with open(FONT_SRC, 'r', encoding='utf-8', errors='replace') as f:
        text = f.read()
    pattern = (
        r'const uint8_t ' + re.escape(font_name) + r'\[[^\]]*\]'
        r'\s*U8G2_FONT_SECTION\([^)]*\)\s*=\s*'
        r'((?:"(?:[^"\\]|\\.)*"\s*)+);'
    )
    m = re.search(pattern, text, re.DOTALL)
    if not m:
        raise RuntimeError(f"未找到字体定义: {font_name}")
    literals = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
    return parse_octal_string(''.join(literals))


def gw(b, off):
    return (b[off] << 8) | b[off+1]

def main():
    src = extract_font_bytes('u8g2_font_wqy12_t_gb2312')
    print(f'gb2312 总字节: {len(src)}')

    # ---- 1. 复制头部 ----
    head = bytearray(src[0:23])
    # 稍后重写 start_pos_unicode
    print('头部:', ' '.join('%02X' % x for x in head))

    # ---- 2. 复制 ASCII 区（23 到 start_pos_unicode 前） ----
    ascii_region = bytes(src[23:23 + gw(src, 21)])
    print(f'ASCII 区: {len(ascii_region)} 字节')

    # ---- 3. 收集 UI 汉字 ----
    ui_chars = ("设置版本退出编辑返回是否探测器报警普通强力接收客户端警告使用"
                "震动次数时长间隔功率")
    # 去重并按码位排序
    needed = sorted(set(ord(c) for c in ui_chars))
    print(f'UI 汉字 ({len(needed)} 个):', ''.join(chr(c) for c in needed))

    # ---- 4. 从 gb2312 提取 glyph 索引 ----
    t = 23 + gw(src, 21)
    off0 = gw(src, t)
    gs = t + off0  # glyph 数据区起点
    # 建立 enc -> 条目字节 的映射（条目 = 2B enc + 1B size + data）
    glyph_map = {}
    p = gs
    while p + 3 < len(src):
        enc = gw(src, p)
        size = src[p+2]
        if enc == 0:
            break
        glyph_map[enc] = bytes(src[p:p+size])
        p += size
    print(f'gb2312 glyph 条目: {len(glyph_map)} 条')

    # ---- 5. 检查覆盖 ----
    missing = [c for c in needed if c not in glyph_map]
    if missing:
        print(f'错误: gb2312 中缺少汉字: {"".join(chr(c) for c in missing)}')
        sys.exit(1)

    # ---- 6. 组装 glyph 数据区（按码位升序） ----
    glyph_entries = []
    for enc in needed:
        glyph_entries.append(glyph_map[enc])
    print(f'收集字形: {len(glyph_entries)} 条, 共 {sum(len(e) for e in glyph_entries)} 字节')

    # ---- 7. 构建 unicode 查找表 ----
    # 布局: [头部23][ASCII区][查找表][glyph数据区]
    # 查找表大小 = 4*(n+1)  (n 条 + 1 哨兵)
    #
    # u8g2 的查找算法 (u8g2_font.c, issue 596) 是"累加"语义:
    #   font = 表起点;
    #   do { font += 表条目.offset; e = 表条目.enc; } while (e < encoding);
    #   for(;;) { 读 (enc,size) 线性扫描 glyph 数据区 }
    # 因此第一条 offset 必须 = 表大小(=glyph 数据区相对表起点的偏移)，
    # 其余条目 offset 必须 = 0。这样无论 do-while 累加多少次，font 都
    # 停在 glyph 数据区起点，再由 for 循环线性扫描找到目标字形。
    # 若把每条 offset 写成绝对偏移，累加后会越界到其他数据区（表现为死循环/卡死）。
    n = len(glyph_entries)
    table_size = 4 * (n + 1)
    glyph_start_rel = table_size  # glyph 数据区相对查找表起点的偏移
    table = bytearray()
    for i, (enc, entry) in enumerate(zip(needed, glyph_entries)):
        off = glyph_start_rel if i == 0 else 0
        table.append((off >> 8) & 0xFF)
        table.append(off & 0xFF)
        table.append((enc >> 8) & 0xFF)
        table.append(enc & 0xFF)
    # 哨兵条目: enc=0xFFFF 保证 do-while 必然终止 (offset 必须是 0)
    table.append(0)
    table.append(0)
    table.append(0xFF)
    table.append(0xFF)

    # ---- 8. 组装完整字体 ----
    new_font = bytearray(head)
    new_font += ascii_region
    new_font += table
    for entry in glyph_entries:
        new_font += entry
    # glyph 数据区终止符: u8g2 的线性扫描以 enc==0 结束，缺少会越界
    new_font += b'\x00\x00\x00'

    # 更新头部 start_pos_unicode (offset 21/22)
    spu = len(ascii_region)
    new_font[21] = (spu >> 8) & 0xFF
    new_font[22] = spu & 0xFF
    # glyph_cnt (offset 0)
    new_font[0] = min(255, 115 + n)

    print(f'新字体总大小: {len(new_font)} 字节')
    print(f'start_pos_unicode = {spu}')

    # ---- 9. 自校验 ---- 
    # 模拟 u8g2_font_get_glyph_data 的完整查找算法，验证每个字符
    # 都能被正确找到，且不存在的字符安全返回 NULL（绝不越界）。
    def u8g2_lookup(fb, encoding):
        """模拟 u8g2 的 glyph 查找，返回 (找到, 条目起始偏移)"""
        def rgw(off):
            return (fb[off] << 8) | fb[off+1]
        p = 23  # U8G2_FONT_DATA_STRUCT_SIZE
        if encoding <= 255:
            if encoding >= ord('a'):
                p += rgw(19)
            elif encoding >= ord('A'):
                p += rgw(17)
            while True:
                if p + 1 >= len(fb):
                    return (False, -1)
                if fb[p+1] == 0:
                    return (False, -1)
                if fb[p] == encoding:
                    return (True, p + 2)
                p += fb[p+1]
        else:
            table_start = p + rgw(21)
            p = table_start
            unicode_lookup_table = p
            while True:
                off = rgw(unicode_lookup_table)
                e = rgw(unicode_lookup_table + 2)
                p += off
                unicode_lookup_table += 4
                if not (e < encoding):
                    break
            while True:
                if p + 2 >= len(fb):
                    return (False, -1)
                e = rgw(p)
                if e == 0:
                    return (False, -1)
                if e == encoding:
                    return (True, p + 3)
                p += fb[p+2]
                if p >= len(fb):
                    return (False, -1)

    # ASCII 校验
    ok = True
    for ch in 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789:[]%.-+ ':
        found, pos = u8g2_lookup(new_font, ord(ch))
        if not found:
            print(f'错误: ASCII 字符 {ch!r} 查找失败')
            ok = False
    print(f'自校验 ASCII 查找: {"通过" if ok else "失败"}')

    # 汉字校验
    ok2 = True
    found_chars = []
    for c in ui_chars:
        found, pos = u8g2_lookup(new_font, ord(c))
        if not found:
            print(f'错误: 汉字 {c!r} (U+{ord(c):04X}) 查找失败')
            ok2 = False
        else:
            found_chars.append(c)
    # 不在字体中的字符必须安全返回 NULL
    for c in '一甲乙丙丁爱':
        found, _ = u8g2_lookup(new_font, ord(c))
        if found:
            print(f'错误: 不应存在的字符 {c!r} 竟然被找到了')
            ok2 = False
    print(f'自校验 unicode 字形: {len(found_chars)} 条: {"".join(found_chars)}')
    if set(found_chars) != set(ui_chars) or not ok2:
        print('错误: 字符集不匹配!')
        sys.exit(1)
    print('自校验通过!')

    # ---- 10. 生成 C 文件 ----
    def c_escape(b):
        # 所有字节统一用 3 位八进制转义（与原 u8g2_fonts.c 一致），
        # 每字节恰好 4 个字符，按字节切行不会切断转义序列
        return ''.join('\\%03o' % byte for byte in b)

    data = c_escape(bytes(new_font))
    # 每行固定字节数分组（每字节 4 字符）
    BYTES_PER_LINE = 24
    lines = []
    for i in range(0, len(new_font), BYTES_PER_LINE):
        chunk = bytes(new_font[i:i+BYTES_PER_LINE])
        lines.append(f'  "{c_escape(chunk)}"')

    with open(OUT_FILE, 'w', encoding='utf-8') as f:
        f.write('// 自定义中文字体子集: 由 tools/gen_font_subset.py 从 u8g2_font_wqy12_t_gb2312 生成\n')
        f.write('// 包含 ASCII 全字符集 + UI 所需汉字\n')
        f.write('#include "u8g2.h"\n\n')
        # 数组不指定大小: C 字符串字面量隐含末尾 NUL，[] 让编译器自动分配(数据+1)
        f.write(f'const uint8_t {FONT_NAME}[] U8G2_FONT_SECTION("{FONT_NAME}") = \n')
        f.write('\n'.join(lines))
        f.write(';\n')
    print(f'已生成 {OUT_FILE}')

if __name__ == '__main__':
    main()
