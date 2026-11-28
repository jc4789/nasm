/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) Nocturne contributors. */

/*
 * outnos.c：.NOS のオブジェクト（NOS_KIND_OBJECT、x86-64）を書く出力形式 -f nos。形式の決まりは nos.h（Nocturne の
 * common/nos.h のコピー）にあり、書き方の説明は Nocturne の ports/nasm/README.md にある。
 *
 * NASM はシンボルを「セグメントとオフセット」で渡し、どのシンボルを書いたかは渡さない。そのため、参照先に global か
 * weak のシンボルがちょうどあればそのシンボルを、なければセクションのシンボルを、リロケーションの参照先にする
 * （weak の上書きと、グループの重複の削除が、参照を正しく付け替えられるように）。MERGE のセクションを指す参照は、
 * シンボルの要素を指し、addend はその要素からの差になる（nos.h）ので、セクションのシンボルを使わず、その位置のラベルか、
 * その位置に置いた名前の無いローカルのシンボルにする。データの自己相対の値（dd x - $）のオフセットは、x の位置から
 * 行の頭（$）を引いた値で、NASM は $ を渡さないので、参照先の位置が分からない。この参照はセクションのシンボルにする。
 */

#include "compiler.h"

#include "nasm.h"
#include "nasmlib.h"
#include "error.h"
#include "ilog2.h"
#include "insns.h"
#include "saa.h"
#include "raa.h"
#include "rbtree.h"
#include "hashtbl.h"
#include "stdscan.h"
#include "eval.h"
#include "outform.h"
#include "outlib.h"
#include "ver.h"
#include "dwarf.h"
#include "nos.h"

#ifdef OF_NOS

/* セクションのアラインメントの上限（common/nos_read.c が align_shift を 30 までしか受けない）。 */
#define NOS_ALIGN_SHIFT_MAX 30u

/* -g の行番号のプログラムの定数（DWARF 5 の 6.2.5.1）。 */
#define NOS_LINE_BASE (-5)
#define NOS_LINE_RANGE 14
#define NOS_OPCODE_BASE 13

struct nos_sym;

struct nos_row {
    uint64_t offset;            /* セクションの中のオフセット */
    uint32_t file;              /* 行番号のファイルの表の番号（0 は主のファイル） */
    int32_t line;
};

struct nos_sect {
    struct nos_sect *next;      /* SECTIONS の順 */
    char *name;
    uint32_t index;             /* SECTIONS の番号 */
    int32_t segment;            /* NASM のセグメント番号 */
    enum nos_role role;
    uint32_t align_shift;
    uint32_t flags;             /* NOS_SECTION_* */
    uint32_t entry_bytes;
    uint32_t priority;
    uint32_t dwarf_part;        /* DEBUG の enum nos_dwarf。ほかは 0 */
    char *group;                /* group= のシンボルの名前 */
    char *parent;               /* parent= のセクションの名前 */
    struct SAA *data;           /* 中身。ZERO と TLS_ZERO は NULL */
    uint64_t bytes;
    struct SAA *relocs;         /* struct nos_reloc */
    uint64_t reloc_count;
    struct rbtree *globals;     /* 中に定義した global と weak のシンボル。キーは値 */
    struct rbtree *locals;      /* MERGE のセクションの中のローカルのシンボル。キーは値 */
    struct SAA *rows;           /* -g の行（struct nos_row）。CODE だけ */
    struct nos_row last_row;
    uint64_t row_count;
    int32_t line;               /* 宣言した場所（書き出しの時のエラーに使う） */
    const char *file;

    /* 書き出しで決まる */
    uint32_t name_offset;
    uint32_t group_number;      /* GROUPS の番号 + 1 */
    uint32_t parent_number;     /* SECTIONS の番号 + 1 */
    uint64_t offset;            /* 中身の、スライスの先頭からのオフセット */
    uint64_t reloc_first;
};

struct nos_sym {
    struct rbtree node;         /* セクションの globals か locals の木（先頭に置き、木の節からキャストで戻る） */
    struct nos_sym *next;       /* SYMBOLS の順 */
    const char *name;           /* ラベルの管理が持つ文字列 */
    struct nos_sect *section;   /* NULL は未定義か絶対 */
    bool absolute;
    uint64_t value;
    uint64_t size;
    enum nos_binding binding;
    enum nos_symbol_type type;
    enum nos_visibility visibility;
    char *size_text;            /* 大きさの式。書き出しの前に評価する */
    int32_t line;               /* 定義か参照の場所（書き出しの前の確かめのエラーを示す） */
    const char *file;

    /* 書き出しで決まる */
    uint32_t number;            /* SYMBOLS の番号 */
    uint32_t name_offset;
};

/* リロケーションの参照先は、セクションのシンボル（section）か、ほかのシンボル（symbol）。 */
struct nos_reloc {
    uint64_t offset;
    int64_t addend;
    enum nos_x86_64_relocation kind;
    struct nos_sect *section;
    struct nos_sym *symbol;
};

struct nos_target {
    struct nos_sect *section;
    struct nos_sym *symbol;
    int64_t delta;              /* 参照先からのずれ */
};

/* section 指令の属性。*_given は書かれたかどうか。 */
struct nos_attrs {
    bool role_given;
    enum nos_role role;
    uint32_t dwarf_part;
    uint64_t align;             /* 0 は書かれていない */
    bool retain;
    uint32_t merge_bytes;       /* merge= か strings= の大きさ。0 は書かれていない */
    bool strings;
    bool priority_given;
    uint32_t priority;
    const char *group;
    const char *parent;
};

/* global・extern・static の、名前の後ろの「:」の後ろ。 */
struct nos_special {
    bool type_given;
    enum nos_symbol_type type;
    bool visibility_given;
    enum nos_visibility visibility;
    bool weak;
    const char *size;           /* 大きさの式の始まり。NULL は書かれていない */
};

static struct nos_sect *nos_sections;
static struct nos_sect **nos_sections_tail;
static uint32_t nos_section_count;
static struct hash_table nos_section_names;
static struct RAA *nos_section_segments; /* セグメント番号 / 2 → struct nos_sect */
static int32_t nos_default_segment;      /* .text。使われた時に作る */

static struct nos_sym *nos_symbols;
static struct nos_sym **nos_symbols_tail;
static struct RAA *nos_undefined_segments; /* 擬似セグメント番号 / 2 → 未定義の struct nos_sym */

static int32_t nos_slot_segment;         /* ..slot（wrt ..slot）のセグメントのベース */

/* -g の行番号のファイルの表。番号 0 は主のファイル（inname）、ほかのファイルは名前 → 番号（1 から） */
static struct hash_table nos_line_file_names;
static struct SAA *nos_line_files;       /* const char *。番号 1 が先頭 */
static uint32_t nos_line_file_count;

/* ---- 役割と名前 ---- */

static const struct {
    const char *keyword;
    enum nos_role role;
} nos_role_keywords[] = {
    {"code", NOS_ROLE_CODE},       {"constant", NOS_ROLE_CONSTANT}, {"data", NOS_ROLE_DATA},
    {"zero", NOS_ROLE_ZERO},       {"tls_data", NOS_ROLE_TLS_DATA}, {"tls_zero", NOS_ROLE_TLS_ZERO},
    {"init", NOS_ROLE_INIT},       {"fini", NOS_ROLE_FINI},         {"unwind", NOS_ROLE_UNWIND},
    {"blob", NOS_ROLE_BLOB},
};

/* 役割を書かないセクションの、名前から決める役割。名前がこの名前か「この名前.」で始まるときに使う。 */
static const struct {
    const char *name;
    enum nos_role role;
} nos_role_names[] = {
    {".text", NOS_ROLE_CODE},      {".rodata", NOS_ROLE_CONSTANT},  {".data", NOS_ROLE_DATA},
    {".bss", NOS_ROLE_ZERO},       {".tdata", NOS_ROLE_TLS_DATA},   {".tbss", NOS_ROLE_TLS_ZERO},
    {".init_array", NOS_ROLE_INIT}, {".fini_array", NOS_ROLE_FINI}, {".eh_frame", NOS_ROLE_UNWIND},
};

/* DWARF の部分の名前（DWARF 5 の 7.1 のセクション名から .debug_ を除いたもの）。 */
static const char *const nos_dwarf_names[] = {
    [NOS_DWARF_INFO] = "info",         [NOS_DWARF_ABBREV] = "abbrev",
    [NOS_DWARF_LINE] = "line",         [NOS_DWARF_LINE_STR] = "line_str",
    [NOS_DWARF_STR] = "str",           [NOS_DWARF_STR_OFFSETS] = "str_offsets",
    [NOS_DWARF_ADDR] = "addr",         [NOS_DWARF_RNGLISTS] = "rnglists",
    [NOS_DWARF_LOCLISTS] = "loclists", [NOS_DWARF_ARANGES] = "aranges",
    [NOS_DWARF_NAMES] = "names",       [NOS_DWARF_FRAME] = "frame",
    [NOS_DWARF_MACRO] = "macro",       [NOS_DWARF_RANGES] = "ranges",
    [NOS_DWARF_LOC] = "loc",           [NOS_DWARF_TYPES] = "types",
    [NOS_DWARF_PUBNAMES] = "pubnames", [NOS_DWARF_PUBTYPES] = "pubtypes",
};

static const char *const nos_declaration_names[] = {
    NOS_SECTION_REQUIRES, NOS_SECTION_PROVIDES, NOS_SECTION_DRIVER, NOS_SECTION_RULES,
};

static const char *nos_role_name(enum nos_role role)
{
    switch (role) {
    case NOS_ROLE_CODE:
        return "code";
    case NOS_ROLE_CONSTANT:
        return "constant";
    case NOS_ROLE_DATA:
        return "data";
    case NOS_ROLE_ZERO:
        return "zero";
    case NOS_ROLE_TLS_DATA:
        return "tls_data";
    case NOS_ROLE_TLS_ZERO:
        return "tls_zero";
    case NOS_ROLE_INIT:
        return "init";
    case NOS_ROLE_FINI:
        return "fini";
    case NOS_ROLE_UNWIND:
        return "unwind";
    case NOS_ROLE_DEBUG:
        return "debug";
    case NOS_ROLE_DECLARATION:
        return "declaration";
    case NOS_ROLE_BLOB:
        return "blob";
    }
    panic();
}

/* ファイルに中身を持たない役割。 */
static bool nos_role_empty(enum nos_role role)
{
    return role == NOS_ROLE_ZERO || role == NOS_ROLE_TLS_ZERO;
}

/* イメージのアドレスを持たない（マップしない）役割。 */
static bool nos_role_unmapped(enum nos_role role)
{
    return role == NOS_ROLE_DEBUG || role == NOS_ROLE_DECLARATION || role == NOS_ROLE_BLOB;
}

static bool nos_role_tls(enum nos_role role)
{
    return role == NOS_ROLE_TLS_DATA || role == NOS_ROLE_TLS_ZERO;
}

static uint32_t nos_role_align_shift(enum nos_role role)
{
    switch (role) {
    case NOS_ROLE_CODE:
        return 4;
    case NOS_ROLE_CONSTANT:
    case NOS_ROLE_DATA:
    case NOS_ROLE_ZERO:
    case NOS_ROLE_TLS_DATA:
    case NOS_ROLE_TLS_ZERO:
        return 2;
    case NOS_ROLE_INIT:
    case NOS_ROLE_FINI:
    case NOS_ROLE_UNWIND:
    case NOS_ROLE_DECLARATION:
        return 3;
    case NOS_ROLE_DEBUG:
    case NOS_ROLE_BLOB:
        return 0;
    }
    panic();
}

static uint32_t nos_dwarf_part(const char *name)
{
    for (uint32_t part = NOS_DWARF_INFO; part <= NOS_DWARF_PUBTYPES; part++)
        if (!strcmp(name, nos_dwarf_names[part]))
            return part;
    return 0;
}

/* name が prefix そのものなら ""、「prefix.」で始まるならその後ろ、どちらでもなければ NULL。 */
static const char *nos_name_suffix(const char *name, const char *prefix)
{
    size_t n = strlen(prefix);

    if (strncmp(name, prefix, n))
        return NULL;
    if (name[n] == '\0')
        return name + n;
    if (name[n] == '.')
        return name + n + 1;
    return NULL;
}

static bool nos_number(const char *text, uint64_t limit, uint64_t *out)
{
    bool error;
    int64_t v;

    if (!text || !*text)
        return false;
    v = readnum(text, &error);
    if (error || v < 0 || (uint64_t)v > limit)
        return false;
    *out = v;
    return true;
}

/* ---- セクション ---- */

static struct nos_sect *nos_sect_by_name(const char *name)
{
    void **p = hash_find(&nos_section_names, name, NULL);
    return p ? *p : NULL;
}

static struct nos_sect *nos_sect_new(const char *name, int32_t segment, const struct nos_attrs *a)
{
    struct hash_insert hi;
    struct nos_sect *s;

    nasm_new(s);
    s->name = nasm_strdup(name);
    s->index = nos_section_count++;
    s->segment = segment;
    s->role = a->role;
    s->dwarf_part = a->dwarf_part;
    s->align_shift = a->align ? ilog2_64(a->align) : nos_role_align_shift(a->role);
    s->flags = (a->retain ? NOS_SECTION_RETAIN : 0) | (a->merge_bytes ? NOS_SECTION_MERGE : 0) |
               (a->strings ? NOS_SECTION_STRINGS : 0);
    s->entry_bytes = a->merge_bytes;
    s->priority = a->priority;
    s->group = a->group ? nasm_strdup(a->group) : NULL;
    s->parent = a->parent ? nasm_strdup(a->parent) : NULL;
    if (!nos_role_empty(a->role))
        s->data = saa_init(1);
    s->relocs = saa_init(sizeof(struct nos_reloc));
    src_get(&s->line, &s->file);

    *nos_sections_tail = s;
    nos_sections_tail = &s->next;
    hash_find(&nos_section_names, s->name, &hi);
    hash_add(&hi, s->name, s);
    nos_section_segments = raa_write_ptr(nos_section_segments, segment >> 1, s);
    return s;
}

/* NASM のセグメントのセクション。既定のセクション .text は、ここで初めて使われた時に作る。 */
static struct nos_sect *nos_sect_at(int32_t segment)
{
    struct nos_sect *s;

    if (segment == NO_SEG || (segment & 1))
        return NULL;
    s = raa_read_ptr(nos_section_segments, segment >> 1);
    if (!s && segment == nos_default_segment) {
        struct nos_attrs a = {.role_given = true, .role = NOS_ROLE_CODE};
        s = nos_sect_new(".text", segment, &a);
    }
    return s;
}

/* key=value の一つを a に入れる。 */
static bool nos_attr(const char *key, const char *value, struct nos_attrs *a)
{
    enum nos_role role = 0;
    uint32_t part = 0;
    uint64_t n;

    for (size_t i = 0; i < ARRAY_SIZE(nos_role_keywords); i++)
        if (!nasm_stricmp(key, nos_role_keywords[i].keyword))
            role = nos_role_keywords[i].role;
    if (!nasm_stricmp(key, "debug")) {
        role = NOS_ROLE_DEBUG;
        part = value ? nos_dwarf_part(value) : 0;
        if (!part) {
            nasm_nonfatal("debug= には DWARF の部分の名前（info、line など）が要る");
            return false;
        }
    } else if (role && value) {
        nasm_nonfatal("セクションの役割 `%s' は値を取らない", key);
        return false;
    }
    if (role) {
        if (a->role_given) {
            nasm_nonfatal("セクションの役割が二つ書かれている");
            return false;
        }
        a->role_given = true;
        a->role = role;
        a->dwarf_part = part;
        return true;
    }

    if (!nasm_stricmp(key, "retain")) {
        if (value) {
            nasm_nonfatal("セクションの属性 `retain' は値を取らない");
            return false;
        }
        a->retain = true;
        return true;
    }
    if (!nasm_stricmp(key, "group") || !nasm_stricmp(key, "parent")) {
        if (!value || !*value) {
            nasm_nonfatal("セクションの属性 `%s' には名前が要る", key);
            return false;
        }
        if (!nasm_stricmp(key, "group"))
            a->group = value;
        else
            a->parent = value;
        return true;
    }
    if (!nasm_stricmp(key, "align")) {
        if (!nos_number(value, UINT64_C(1) << NOS_ALIGN_SHIFT_MAX, &n) || !is_power2(n)) {
            nasm_nonfatal("align= は 2 の冪で、2^%u 以下", NOS_ALIGN_SHIFT_MAX);
            return false;
        }
        a->align = n;
        return true;
    }
    if (!nasm_stricmp(key, "merge") || !nasm_stricmp(key, "strings")) {
        if (!nos_number(value, UINT32_MAX, &n) || !n) {
            nasm_nonfatal("%s= には 1 以上の要素の大きさが要る", key);
            return false;
        }
        a->merge_bytes = n;
        a->strings = !nasm_stricmp(key, "strings");
        return true;
    }
    if (!nasm_stricmp(key, "priority")) {
        if (!nos_number(value, UINT32_MAX, &n)) {
            nasm_nonfatal("priority= には数が要る");
            return false;
        }
        a->priority_given = true;
        a->priority = n;
        return true;
    }
    nasm_nonfatal("知らないセクションの属性 `%s'", key);
    return false;
}

/* section 指令の名前の後ろの属性を読む。p は書き換える。 */
static bool nos_attrs_parse(char *p, struct nos_attrs *a)
{
    nasm_zero(*a);
    while (*(p = nasm_skip_spaces(p))) {
        char *end = nasm_skip_word(p);
        char *value;

        if (*end)
            *end++ = '\0';
        value = strchr(p, '=');
        if (value)
            *value++ = '\0';
        if (!nos_attr(p, value, a))
            return false;
        p = end;
    }
    return true;
}

/* 役割を書かないセクションの役割を、名前から決める。 */
static bool nos_role_from_name(const char *name, struct nos_attrs *a)
{
    const char *suffix;
    uint64_t priority;

    for (size_t i = 0; i < ARRAY_SIZE(nos_role_names); i++) {
        suffix = nos_name_suffix(name, nos_role_names[i].name);
        if (!suffix)
            continue;
        a->role = nos_role_names[i].role;
        if ((a->role == NOS_ROLE_INIT || a->role == NOS_ROLE_FINI) && *suffix) {
            if (a->priority_given || !nos_number(suffix, UINT32_MAX, &priority)) {
                nasm_nonfatal("セクション `%s' の名前の後ろは優先度の数", name);
                return false;
            }
            a->priority_given = true;
            a->priority = priority;
        }
        return true;
    }
    if (!strncmp(name, ".debug_", 7)) {
        a->dwarf_part = nos_dwarf_part(name + 7);
        a->role = NOS_ROLE_DEBUG;
        if (a->dwarf_part)
            return true;
    }
    nasm_nonfatal("セクション `%s' の役割が名前から決まらないので、役割を属性で書く", name);
    return false;
}

/* 新しいセクションの役割と属性を決める。 */
static bool nos_attrs_complete(const char *name, struct nos_attrs *a)
{
    if (!strncmp(name, "nos.", 4)) {
        bool known = false;

        for (size_t i = 0; i < ARRAY_SIZE(nos_declaration_names); i++)
            known |= !strcmp(name, nos_declaration_names[i]);
        if (!known) {
            nasm_nonfatal("名前が nos. で始まるセクションは nos.requires・nos.provides・nos.driver・nos.rules のどれか");
            return false;
        }
        if (a->role_given) {
            nasm_nonfatal("名前が nos. で始まるセクションの役割は declaration で、属性で書けない");
            return false;
        }
        a->role = NOS_ROLE_DECLARATION;
    } else if (!a->role_given && !nos_role_from_name(name, a)) {
        return false;
    }

    if (a->priority_given && a->role != NOS_ROLE_INIT && a->role != NOS_ROLE_FINI) {
        nasm_nonfatal("priority= は init と fini のセクションだけ");
        return false;
    }
    if (!a->priority_given && (a->role == NOS_ROLE_INIT || a->role == NOS_ROLE_FINI))
        a->priority = NOS_PRIORITY_DEFAULT;
    if (a->merge_bytes && nos_role_empty(a->role)) {
        nasm_nonfatal("%s のセクションは merge と strings を持てない", nos_role_name(a->role));
        return false;
    }
    return true;
}

static bool nos_name_equal(const char *a, const char *b)
{
    return (!a && !b) || (a && b && !strcmp(a, b));
}

/* 宣言し直しの属性は、書いたものが前の宣言と同じでなければならない。アラインメントは大きくだけなる。 */
static bool nos_sect_redeclare(struct nos_sect *s, const struct nos_attrs *a)
{
    bool same = true;

    if (a->role_given)
        same &= a->role == s->role && a->dwarf_part == s->dwarf_part;
    if (a->retain)
        same &= !!(s->flags & NOS_SECTION_RETAIN);
    if (a->merge_bytes)
        same &= a->merge_bytes == s->entry_bytes && a->strings == !!(s->flags & NOS_SECTION_STRINGS);
    if (a->priority_given)
        same &= a->priority == s->priority;
    if (a->group)
        same &= nos_name_equal(a->group, s->group);
    if (a->parent)
        same &= nos_name_equal(a->parent, s->parent);
    if (!same) {
        nasm_nonfatal("セクション `%s' の属性が前の宣言と違う", s->name);
        return false;
    }
    if (a->align && ilog2_64(a->align) > s->align_shift)
        s->align_shift = ilog2_64(a->align);
    return true;
}

static int32_t nos_section(char *name, int *bits)
{
    struct nos_attrs a;
    struct nos_sect *s;
    char *attrs;

    if (!name) {
        *bits = 64;
        return nos_default_segment;
    }

    attrs = nasm_skip_word(name);
    if (*attrs)
        *attrs++ = '\0';
    if (!nos_attrs_parse(attrs, &a))
        return NO_SEG;

    s = nos_sect_by_name(name);
    if (s)
        return nos_sect_redeclare(s, &a) ? s->segment : NO_SEG;
    if (!nos_attrs_complete(name, &a))
        return NO_SEG;
    s = nos_sect_new(name, !strcmp(name, ".text") ? nos_default_segment : seg_alloc(), &a);
    return s->segment;
}

static void nos_sectalign(int32_t segment, unsigned int value)
{
    struct nos_sect *s = nos_sect_at(segment);
    uint32_t shift = ilog2_32(value);

    if (s && shift > s->align_shift)
        s->align_shift = shift;
}

/* ---- シンボル ---- */

static bool nos_word(const char *p, size_t n, const char *word)
{
    return n == strlen(word) && !nasm_strnicmp(p, word, n);
}

/* 名前の後ろの「:」の後ろを読む。型と可視性と weak の後ろは大きさの式。 */
static bool nos_special_parse(const char *name, const char *special, struct nos_special *sp)
{
    const char *p = nasm_skip_spaces(special ? special : "");

    nasm_zero(*sp);
    while (*p) {
        const char *end = nasm_skip_word(p);
        size_t n = end - p;
        bool twice = false;

        if (nos_word(p, n, "function") || nos_word(p, n, "data") || nos_word(p, n, "tls")) {
            twice = sp->type_given;
            sp->type_given = true;
            sp->type = nos_word(p, n, "function") ? NOS_SYMBOL_FUNCTION
                       : nos_word(p, n, "data")   ? NOS_SYMBOL_DATA
                                                  : NOS_SYMBOL_TLS;
        } else if (nos_word(p, n, "image") || nos_word(p, n, "exportable")) {
            twice = sp->visibility_given;
            sp->visibility_given = true;
            sp->visibility = nos_word(p, n, "image") ? NOS_VISIBILITY_IMAGE : NOS_VISIBILITY_EXPORTABLE;
        } else if (nos_word(p, n, "weak")) {
            twice = sp->weak;
            sp->weak = true;
        } else {
            sp->size = p;
            break;
        }
        if (twice) {
            nasm_nonfatal("シンボル `%s' の型か可視性か weak が二つ書かれている", name);
            return false;
        }
        p = nasm_skip_spaces(end);
    }
    return true;
}

/* sp の型と大きさを、定義された場所（s。NULL は未定義、absolute は絶対）に合わせて y に入れる。 */
static bool nos_sym_type(struct nos_sym *y, const struct nos_special *sp, bool undefined)
{
    bool tls = y->section && nos_role_tls(y->section->role);

    y->type = sp->type_given ? sp->type : tls ? NOS_SYMBOL_TLS : NOS_SYMBOL_NONE;
    if (tls && y->type != NOS_SYMBOL_TLS) {
        nasm_nonfatal("TLS のセクションのシンボル `%s' の型は tls", y->name);
        return false;
    }
    if (!tls && !undefined && y->type == NOS_SYMBOL_TLS) {
        nasm_nonfatal("シンボル `%s' は TLS のセクションの外にあるので、型を tls にできない", y->name);
        return false;
    }
    if (sp->size && undefined) {
        nasm_nonfatal("未定義のシンボル `%s' に大きさは書けない", y->name);
        return false;
    }
    if (sp->size)
        y->size_text = nasm_strdup(sp->size);
    return true;
}

static void nos_symdef(char *name, int32_t segment, int64_t offset, int is_global, char *special)
{
    struct nos_special sp;
    struct nos_sect *s;
    struct nos_sym *y;
    bool undefined;

    if (name[0] == '.' && name[1] == '.') {
        if (name[2] != '@' && strcmp(name, "..slot"))
            nasm_nonfatal("知らない特別なシンボル `%s'", name);
        return; /* NASM の特別なシンボルとマクロの中のラベルは、表に入れない */
    }
    if (is_global == 3)
        return; /* 最後のパスの大きさの直しは使わない（大きさは書き出しの前に評価する） */
    if (is_global == 2) {
        nasm_nonfatal("common のシンボル `%s' は .NOS に無い", name);
        return;
    }
    if (segment != NO_SEG && (segment & 1)) {
        nasm_nonfatal("シンボル `%s' の値はセグメントで、.NOS に無い", name);
        return;
    }

    s = nos_sect_at(segment);
    undefined = !s && segment != NO_SEG;
    if (undefined && raa_read_ptr(nos_undefined_segments, segment >> 1)) {
        if (is_global)
            nasm_nonfatal("`%s' は未定義のシンボルの別名で、.NOS のシンボルの表に書けない", name);
        return; /* ローカルの別名への参照は、NASM が元のシンボルへの参照にする */
    }
    if (!is_global && !s)
        return; /* ローカルの絶対シンボル（equ の定数）はアドレスでないので、表に入れない */
    if (!nos_special_parse(name, special, &sp))
        return;
    if (!is_global && (sp.weak || sp.visibility_given)) {
        nasm_nonfatal("ローカルのシンボル `%s' に weak・image・exportable は書けない", name);
        return;
    }
    if (undefined && sp.visibility_given) {
        nasm_nonfatal("未定義のシンボル `%s' に image・exportable は書けない", name);
        return;
    }

    nasm_new(y);
    y->name = name;
    y->section = s;
    y->absolute = segment == NO_SEG;
    y->value = undefined ? 0 : offset;
    src_get(&y->line, &y->file);
    y->binding = !is_global ? NOS_BINDING_LOCAL : sp.weak ? NOS_BINDING_WEAK : NOS_BINDING_GLOBAL;
    /* 見え方は、定義された GLOBAL と WEAK のシンボルだけが意味を持つ（common/nos.h の enum nos_visibility） */
    y->visibility = undefined ? NOS_VISIBILITY_EXPORTABLE
                    : !is_global ? NOS_VISIBILITY_IMAGE
                    : sp.visibility_given ? sp.visibility
                                          : NOS_VISIBILITY_EXPORTABLE;
    if (!nos_sym_type(y, &sp, undefined)) {
        nasm_free(y);
        return;
    }

    if (s && y->binding != NOS_BINDING_LOCAL) {
        y->node.key = y->value;
        s->globals = rb_insert(s->globals, &y->node);
    } else if (s && (s->flags & NOS_SECTION_MERGE)) {
        y->node.key = y->value;
        s->locals = rb_insert(s->locals, &y->node);
    }
    if (undefined)
        nos_undefined_segments = raa_write_ptr(nos_undefined_segments, segment >> 1, y);
    *nos_symbols_tail = y;
    nos_symbols_tail = &y->next;
}

/* ---- 出力 ---- */

static bool nos_all_zero(const uint8_t *p, uint64_t n)
{
    while (n--)
        if (*p++)
            return false;
    return true;
}

/* 中身を足す。data が NULL なら 0 を足す。 */
static void nos_put(struct nos_sect *s, const void *data, uint64_t size)
{
    if (!nos_role_empty(s->role))
        saa_wbytes(s->data, data, size);
    else if (data && !nos_all_zero(data, size))
        nasm_nonfatal("%s のセクション `%s' に 0 でない中身は置けない", nos_role_name(s->role), s->name);
    s->bytes += size;
}

static void nos_reloc_add(struct nos_sect *s, const struct nos_reloc *r)
{
    struct nos_reloc *e = saa_wstruct(s->relocs);

    *e = *r;
    s->reloc_count++;
}

/* MERGE のセクション s の offset を指すシンボル：その位置のラベルか、無ければ名前の無いローカルのシンボルを置く。 */
static struct nos_sym *nos_merge_anchor(struct nos_sect *s, int64_t offset, bool located)
{
    struct rbtree *local;
    struct nos_sym *y;

    if (!located) {
        nasm_nonfatal("MERGE のセクション `%s' を指すデータの自己相対の値は、参照先の位置が分からないので書けない", s->name);
        return NULL;
    }
    if (offset < 0) {
        nasm_nonfatal("MERGE のセクション `%s' の前を指す参照", s->name);
        return NULL;
    }
    local = rb_search_exact(s->locals, offset);
    if (local)
        return (struct nos_sym *)local;
    nasm_new(y);
    y->name = "";
    y->section = s;
    y->value = offset;
    y->binding = NOS_BINDING_LOCAL;
    y->type = NOS_SYMBOL_NONE;
    y->visibility = NOS_VISIBILITY_IMAGE;
    src_get(&y->line, &y->file);
    y->node.key = y->value;
    s->locals = rb_insert(s->locals, &y->node);
    *nos_symbols_tail = y;
    nos_symbols_tail = &y->next;
    return y;
}

/* 参照先を決める。located は offset が参照先の位置であること（データの自己相対の値は位置でない）、exact は参照先に
   ちょうど一致するシンボルが要る参照（スロット。命令の参照なので located）。 */
static bool nos_target(int32_t segment, int64_t offset, bool located, bool exact, struct nos_target *t)
{
    struct nos_sect *s = nos_sect_at(segment);
    struct rbtree *global;

    nasm_zero(*t);
    if (segment == NO_SEG) {
        nasm_nonfatal("絶対アドレスへの相対の参照とスロットの参照は .NOS に無い");
        return false;
    }
    if (!s) {
        t->symbol = raa_read_ptr(nos_undefined_segments, segment >> 1);
        nasm_assert(t->symbol);
        t->delta = offset;
        return true;
    }
    global = located ? rb_search_exact(s->globals, offset) : NULL;
    if (global) {
        t->symbol = (struct nos_sym *)global;
        return true;
    }
    if (exact && offset) {
        nasm_nonfatal("..slot の参照先は、global か weak のシンボルか、セクションの先頭");
        return false;
    }
    if (s->flags & NOS_SECTION_MERGE) {
        t->symbol = nos_merge_anchor(s, offset, located);
        return t->symbol != NULL;
    }
    t->section = s;
    t->delta = offset;
    return true;
}

/* 参照先の役割が、この参照を許すか。 */
static bool nos_target_allows(const struct nos_target *t, const struct out_data *d)
{
    const struct nos_sect *s = t->section ? t->section : t->symbol->section;
    const char *name = t->section ? t->section->name : t->symbol->name;

    if ((s && nos_role_tls(s->role)) || (!s && t->symbol->type == NOS_SYMBOL_TLS)) {
        nasm_nonfatal("TLS の%s `%s' を参照する書き方は -f nos に無い", t->section ? "セクション" : "シンボル", name);
        return false;
    }
    if (!s || !nos_role_unmapped(s->role))
        return true;
    if (s->role != NOS_ROLE_DEBUG) {
        nasm_nonfatal("マップしないセクション `%s' のアドレスは参照できない", s->name);
        return false;
    }
    if (d->type != OUT_ADDRESS || d->twrt != NO_SEG) {
        nasm_nonfatal("DEBUG のセクション `%s' への参照は、その中のオフセット（4 byte か 8 byte の絶対の値）だけ", s->name);
        return false;
    }
    return true;
}

/* mov r64, [rip + x] か。命令のテンプレートで見る（リンカーは S がイメージの中なら lea にする）。 */
static bool nos_slot_load(const struct out_data *d)
{
    return d->itemp->opcode == I_MOV && is_class(REG_GPR | BITS64, d->itemp->opd[0]);
}

static bool nos_kind_absolute(const struct nos_target *t, const struct out_data *d, enum nos_x86_64_relocation *kind)
{
    const struct nos_sect *s = t->section ? t->section : t->symbol->section;
    bool debug = s && s->role == NOS_ROLE_DEBUG;

    if (d->size == 8) {
        *kind = debug ? NOS_X86_64_SECTION64 : NOS_X86_64_ABS64;
        return true;
    }
    if (d->size != 4) {
        nasm_nonfatal("%d byte の絶対の参照は .NOS に無い", (int)d->size);
        return false;
    }
    if (debug) {
        *kind = NOS_X86_64_SECTION32;
        return true;
    }
    if (s) {
        nasm_nonfatal("イメージの中のアドレスは 32 bit の絶対の値に入らない（32 bit の絶対の参照は絶対シンボルだけ）");
        return false;
    }
    *kind = (d->flags & OUT_SIGNED) ? NOS_X86_64_ABS32S : NOS_X86_64_ABS32;
    return true;
}

static bool nos_kind_relative(const struct out_data *d, enum nos_x86_64_relocation *kind)
{
    if (d->twrt == nos_slot_segment) {
        *kind = nos_slot_load(d) ? NOS_X86_64_SLOT32_LOAD : NOS_X86_64_SLOT32;
        return true;
    }
    if (d->size == 4) {
        *kind = (d->flags & OUT_BRANCH) ? NOS_X86_64_BRANCH32 : NOS_X86_64_PC32;
        return true;
    }
    if (d->size == 8) {
        *kind = NOS_X86_64_PC64;
        return true;
    }
    nasm_nonfatal("%d byte の相対の参照は .NOS に無い", (int)d->size);
    return false;
}

/* d の参照を s のリロケーションにする。フィールドの値はリンカーが書くので、中身は 0。 */
static void nos_reference(struct nos_sect *s, const struct out_data *d)
{
    bool instruction = d->itemp; /* db・dd などのデータは命令のテンプレートを持たない */
    bool slot = d->twrt == nos_slot_segment;
    struct nos_target t;
    struct nos_reloc r;
    bool ok;

    nos_put(s, NULL, d->size);
    if (nos_role_empty(s->role) || s->role == NOS_ROLE_DECLARATION || s->role == NOS_ROLE_BLOB) {
        nasm_nonfatal("%s のセクション `%s' はリロケーションを持てない", nos_role_name(s->role), s->name);
        return;
    }
    if (d->twrt != NO_SEG && d->twrt != nos_slot_segment) {
        nasm_nonfatal("-f nos の wrt に書けるのは ..slot だけ");
        return;
    }
    if (slot && (d->type != OUT_RELADDR || !instruction || d->size != 4 || (d->flags & OUT_BRANCH))) {
        nasm_nonfatal("wrt ..slot は、命令の 4 byte の RIP 相対の参照（分岐の行き先でないもの）だけ");
        return;
    }
    if (!nos_target(d->tsegment, d->toffset, d->type == OUT_ADDRESS || instruction, slot, &t) ||
        !nos_target_allows(&t, d))
        return;

    nasm_zero(r);
    r.offset = d->loc.offset;
    r.section = t.section;
    r.symbol = t.symbol;
    if (d->type == OUT_ADDRESS) {
        ok = nos_kind_absolute(&t, d, &r.kind);
        r.addend = t.delta;
    } else {
        ok = nos_kind_relative(d, &r.kind);
        r.addend = t.delta + d->loc.offset - d->relbase;
    }
    if (ok)
        nos_reloc_add(s, &r);
}

static void nos_out(const struct out_data *d)
{
    struct nos_sect *s = nos_sect_at(d->loc.segment);

    nasm_assert(s && (uint64_t)d->loc.offset == s->bytes);
    switch (d->type) {
    case OUT_RAWDATA:
        nos_put(s, d->data, d->size);
        break;
    case OUT_ZERODATA:
        nos_put(s, NULL, d->size);
        break;
    case OUT_RESERVE:
        if (!nos_role_empty(s->role))
            nasm_warn(WARN_ZEROING, "中身を持つセクション `%s' の RES は 0 で埋める", s->name);
        nos_put(s, NULL, d->size);
        break;
    case OUT_ADDRESS:
    case OUT_RELADDR:
        nos_reference(s, d);
        break;
    case OUT_SEGMENT:
        nasm_nonfatal("セグメントの値は .NOS に無い");
        nos_put(s, NULL, d->size);
        break;
    case OUT_REL1ADR:
    case OUT_REL2ADR:
    case OUT_REL4ADR:
    case OUT_REL8ADR:
        panic(); /* 旧い出力の種類は、新しい出力の関数に来ない */
    }
}

/* ---- -g：DWARF 5 の行番号 ---- */

static void nos_dwarf_init(void)
{
    nos_line_files = saa_init(sizeof(const char *));
}

static uint32_t nos_line_file(const char *name)
{
    struct hash_insert hi;
    void **p;
    const char **e;

    if (!strcmp(name, inname))
        return 0;
    p = hash_find(&nos_line_file_names, name, &hi);
    if (p)
        return (uint32_t)(uintptr_t)*p;
    e = saa_wstruct(nos_line_files);
    *e = name;
    nos_line_file_count++;
    hash_add(&hi, name, (void *)(uintptr_t)nos_line_file_count);
    return nos_line_file_count;
}

/* 行番号の表は CODE のセクションの命令だけを書く。行の場所は、続けて出力される中身の場所（今のセクションの大きさ）。 */
static void nos_dwarf_linenum(const char *filename, int32_t line, int32_t segment)
{
    struct nos_sect *s = nos_sect_at(segment);
    struct nos_row *row;
    uint32_t file;

    if (!s || s->role != NOS_ROLE_CODE)
        return;
    file = nos_line_file(filename);
    if (s->row_count && s->last_row.file == file && s->last_row.line == line)
        return;
    if (!s->rows)
        s->rows = saa_init(sizeof(struct nos_row));
    row = saa_wstruct(s->rows);
    row->offset = s->bytes;
    row->file = file;
    row->line = line;
    s->last_row = *row;
    s->row_count++;
}

static struct nos_sect *nos_dwarf_section(const char *name, uint32_t part)
{
    struct nos_attrs a = {.role_given = true, .role = NOS_ROLE_DEBUG, .dwarf_part = part};

    if (nos_sect_by_name(name)) {
        nasm_nonfatal("-g のデバッグ情報が使うセクション名 `%s' のセクションがある", name);
        return NULL;
    }
    return nos_sect_new(name, seg_alloc(), &a);
}

/* DWARF のセクション s に、target（セクションのシンボル）+ addend を指す size byte のリロケーションを書く。 */
static void nos_dwarf_reloc(struct nos_sect *s, enum nos_x86_64_relocation kind, struct nos_sect *target,
                            int64_t addend, unsigned int size)
{
    struct nos_reloc r = {.offset = s->data->datalen, .addend = addend, .kind = kind, .section = target};

    nos_reloc_add(s, &r);
    saa_wbytes(s->data, NULL, size);
}

static void nos_dwarf_string(struct SAA *d, const char *text)
{
    saa_wbytes(d, text, strlen(text) + 1);
}

/* at に置いた 4 byte の長さの欄に、欄の後ろから今の終わりまでの長さを書く。saa_fwrite は書く位置を動かすので、
   終わりへ戻す。 */
static void nos_dwarf_length(struct SAA *d, uint64_t at)
{
    uint64_t end = d->datalen;
    uint32_t length = htole32((uint32_t)(end - at - 4));

    saa_fwrite(d, at, &length, 4);
    saa_fwrite(d, end, NULL, 0);
}

/* unit_length の欄を置き、その位置を返す。中身を書いた後に nos_dwarf_length で長さを入れる。 */
static uint64_t nos_dwarf_unit_begin(struct SAA *d)
{
    uint64_t at = d->datalen;

    saa_write32(d, 0);
    return at;
}

static void nos_dwarf_abbrev(struct SAA *d)
{
    static const unsigned int attributes[][2] = {
        {DW_AT_producer, DW_FORM_string},  {DW_AT_language, DW_FORM_data2},
        {DW_AT_name, DW_FORM_string},      {DW_AT_comp_dir, DW_FORM_string},
        {DW_AT_stmt_list, DW_FORM_sec_offset}, {DW_AT_low_pc, DW_FORM_addr},
        {DW_AT_ranges, DW_FORM_sec_offset},
    };

    saa_wleb128u(d, 1);
    saa_wleb128u(d, DW_TAG_compile_unit);
    saa_write8(d, DW_CHILDREN_no);
    for (size_t i = 0; i < ARRAY_SIZE(attributes); i++) {
        saa_wleb128u(d, attributes[i][0]);
        saa_wleb128u(d, attributes[i][1]);
    }
    saa_wleb128u(d, 0);
    saa_wleb128u(d, 0);
    saa_wleb128u(d, 0);
}

/* コンパイル単位：属性の並びは nos_dwarf_abbrev と同じ順。範囲は rnglists の最初の列（ヘッダーの 12 byte の後ろ）。 */
static void nos_dwarf_info(struct nos_sect *info, struct nos_sect *abbrev, struct nos_sect *line,
                           struct nos_sect *rnglists, const char *directory)
{
    struct SAA *d = info->data;
    uint64_t unit = nos_dwarf_unit_begin(d);

    saa_write16(d, 5);
    saa_write8(d, DW_UT_compile);
    saa_write8(d, 8);
    nos_dwarf_reloc(info, NOS_X86_64_SECTION32, abbrev, 0, 4);
    saa_wleb128u(d, 1);
    nos_dwarf_string(d, nasm_signature());
    saa_write16(d, DW_LANG_Mips_Assembler);
    nos_dwarf_string(d, inname);
    nos_dwarf_string(d, directory);
    nos_dwarf_reloc(info, NOS_X86_64_SECTION32, line, 0, 4);
    saa_write64(d, 0);
    nos_dwarf_reloc(info, NOS_X86_64_SECTION32, rnglists, 12, 4);
    nos_dwarf_length(d, unit);
}

static void nos_dwarf_rnglists(struct nos_sect *rnglists)
{
    struct SAA *d = rnglists->data;
    uint64_t unit = nos_dwarf_unit_begin(d);

    saa_write16(d, 5);
    saa_write8(d, 8);
    saa_write8(d, 0);
    saa_write32(d, 0);
    for (struct nos_sect *s = nos_sections; s; s = s->next) {
        if (!s->row_count)
            continue;
        saa_write8(d, DW_RLE_start_length);
        nos_dwarf_reloc(rnglists, NOS_X86_64_ABS64, s, 0, 8);
        saa_wleb128u(d, s->bytes);
    }
    saa_write8(d, DW_RLE_end_of_list);
    nos_dwarf_length(d, unit);
}

/* 行列の一行：アドレスを address_delta、行を line_delta 進めて行を足す（DWARF 5 の 6.2.5.1）。 */
static void nos_dwarf_row(struct SAA *d, uint64_t address_delta, int64_t line_delta)
{
    unsigned int opcode;

    if (line_delta < NOS_LINE_BASE || line_delta >= NOS_LINE_BASE + NOS_LINE_RANGE) {
        saa_write8(d, DW_LNS_advance_line);
        saa_wleb128s(d, line_delta);
        line_delta = 0;
    }
    opcode = (unsigned int)(line_delta - NOS_LINE_BASE) + NOS_OPCODE_BASE;
    if (address_delta <= (255 - opcode) / NOS_LINE_RANGE) {
        saa_write8(d, opcode + address_delta * NOS_LINE_RANGE);
        return;
    }
    saa_write8(d, DW_LNS_advance_pc);
    saa_wleb128u(d, address_delta);
    saa_write8(d, opcode);
}

/* セクション code の行の列。状態の初期値は DWARF 5 の表 6.4（file 1、line 1）。 */
static void nos_dwarf_sequence(struct nos_sect *line, struct nos_sect *code)
{
    struct SAA *d = line->data;
    const struct nos_row *row;
    uint64_t address = 0;
    uint32_t file = 1;
    int64_t line_number = 1;

    saa_write8(d, DW_LNS_extended_op);
    saa_wleb128u(d, 9);
    saa_write8(d, DW_LNE_set_address);
    nos_dwarf_reloc(line, NOS_X86_64_ABS64, code, 0, 8);

    saa_rewind(code->rows);
    while ((row = saa_rstruct(code->rows))) {
        if (row->file != file) {
            saa_write8(d, DW_LNS_set_file);
            saa_wleb128u(d, row->file);
            file = row->file;
        }
        nos_dwarf_row(d, row->offset - address, row->line - line_number);
        address = row->offset;
        line_number = row->line;
    }

    saa_write8(d, DW_LNS_advance_pc);
    saa_wleb128u(d, code->bytes - address);
    saa_write8(d, DW_LNS_extended_op);
    saa_wleb128u(d, 1);
    saa_write8(d, DW_LNE_end_sequence);
}

/* 行番号のプログラムのヘッダー（DWARF 5 の 6.2.4）。ディレクトリは作業のディレクトリ一つで、ファイルの名前は NASM が
   開いた時の名前（相対なら作業のディレクトリから）。ファイル 0 は DW_AT_name と同じ主のファイル。 */
static void nos_dwarf_line_header(struct SAA *d, const char *directory)
{
    static const uint8_t opcode_lengths[NOS_OPCODE_BASE - 1] = {0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1};
    uint64_t header;

    saa_write16(d, 5);
    saa_write8(d, 8);
    saa_write8(d, 0);
    header = d->datalen;
    saa_write32(d, 0);
    saa_write8(d, 1);
    saa_write8(d, 1);
    saa_write8(d, 1);
    saa_write8(d, (uint8_t)NOS_LINE_BASE);
    saa_write8(d, NOS_LINE_RANGE);
    saa_write8(d, NOS_OPCODE_BASE);
    saa_wbytes(d, opcode_lengths, sizeof opcode_lengths);

    saa_write8(d, 1);
    saa_wleb128u(d, DW_LNCT_path);
    saa_wleb128u(d, DW_FORM_string);
    saa_wleb128u(d, 1);
    nos_dwarf_string(d, directory);

    saa_write8(d, 2);
    saa_wleb128u(d, DW_LNCT_path);
    saa_wleb128u(d, DW_FORM_string);
    saa_wleb128u(d, DW_LNCT_directory_index);
    saa_wleb128u(d, DW_FORM_udata);
    saa_wleb128u(d, 1 + nos_line_file_count);
    nos_dwarf_string(d, inname);
    saa_wleb128u(d, 0);
    saa_rewind(nos_line_files);
    for (const char **name; (name = saa_rstruct(nos_line_files));) {
        nos_dwarf_string(d, *name);
        saa_wleb128u(d, 0);
    }

    nos_dwarf_length(d, header);
}

static void nos_dwarf_line(struct nos_sect *line, const char *directory)
{
    struct SAA *d = line->data;
    uint64_t unit = nos_dwarf_unit_begin(d);

    nos_dwarf_line_header(d, directory);
    for (struct nos_sect *s = nos_sections; s; s = s->next)
        if (s->row_count)
            nos_dwarf_sequence(line, s);
    nos_dwarf_length(d, unit);
}

/* 書き出しの前に、行を持つ CODE のセクションのデバッグ情報（abbrev、info、rnglists、line）を作る。 */
static void nos_dwarf_cleanup(void)
{
    struct nos_sect *abbrev, *info, *line, *rnglists;
    bool rows = false;
    char *directory;

    for (struct nos_sect *s = nos_sections; s; s = s->next)
        rows |= s->row_count != 0;
    if (rows) {
        abbrev = nos_dwarf_section(".debug_abbrev", NOS_DWARF_ABBREV);
        info = nos_dwarf_section(".debug_info", NOS_DWARF_INFO);
        line = nos_dwarf_section(".debug_line", NOS_DWARF_LINE);
        rnglists = nos_dwarf_section(".debug_rnglists", NOS_DWARF_RNGLISTS);
        if (abbrev && info && line && rnglists) {
            directory = nasm_realpath(".");
            nos_dwarf_abbrev(abbrev->data);
            nos_dwarf_info(info, abbrev, line, rnglists, directory);
            nos_dwarf_rnglists(rnglists);
            nos_dwarf_line(line, directory);
            abbrev->bytes = abbrev->data->datalen;
            info->bytes = info->data->datalen;
            line->bytes = line->data->datalen;
            rnglists->bytes = rnglists->data->datalen;
            nasm_free(directory);
        }
    }
    saa_free(nos_line_files);
    hash_free(&nos_line_file_names);
}

/* ---- 書き出し ---- */

/* 書き出しの前のシンボルの仕上げ：大きさの式を評価し（全部のラベルの値が決まった後なので、前方の参照も読める）、
   値がセクションの中にあることを確かめる（MERGE のセクションを指すために置いたシンボルは、セクションの外を指しうる）。 */
static void nos_symbols_finish(void)
{
    int32_t line;
    const char *file;

    src_get(&line, &file);
    for (struct nos_sym *y = nos_symbols; y; y = y->next) {
        struct tokenval tv;
        expr *e;

        src_set(y->line, y->file);
        if (y->section && y->value > y->section->bytes)
            nasm_nonfatal("セクション `%s' のオフセット %" PRIu64 " を指す参照が、セクションの外を指す", y->section->name,
                          y->value);
        if (!y->size_text)
            continue;
        stdscan_reset(y->size_text);
        tv.t_type = TOKEN_INVALID;
        e = evaluate(stdscan, NULL, &tv, NULL, true, NULL);
        if (!e)
            continue;
        if (tv.t_type != TOKEN_EOS || !is_simple(e) || reloc_value(e) < 0)
            nasm_nonfatal("シンボル `%s' の大きさは、0 以上の定数の式で書く", y->name);
        else
            y->size = reloc_value(e);
    }
    src_set(line, file);
}

/* STRINGS に text を足し、オフセットを返す。空の名前はオフセット 0（nos_write が先頭に置く NUL）。 */
static uint32_t nos_string(struct SAA *strings, const char *text)
{
    uint32_t at = (uint32_t)strings->datalen;

    if (!*text)
        return 0;
    saa_wbytes(strings, text, strlen(text) + 1);
    return at;
}

/* グループと親を名前から番号にし、MERGE の大きさを確かめる。エラーはセクションを宣言した場所で示す。 */
static void nos_sections_resolve(struct SAA *groups)
{
    uint32_t group_count = 0;
    int32_t line;
    const char *file;

    src_get(&line, &file);
    for (struct nos_sect *s = nos_sections; s; s = s->next) {
        src_set(s->line, s->file);
        if ((s->flags & NOS_SECTION_MERGE) && s->bytes % s->entry_bytes)
            nasm_nonfatal("merge のセクション `%s' の大きさが要素の大きさの倍数でない", s->name);
        if (s->parent) {
            struct nos_sect *p = nos_sect_by_name(s->parent);

            if (!p || p == s)
                nasm_nonfatal("セクション `%s' の親 `%s' がない（自分は親にできない）", s->name, s->parent);
            else
                s->parent_number = p->index + 1;
        }
        if (!s->group)
            continue;
        for (struct nos_sect *t = nos_sections; t != s; t = t->next)
            if (t->group && !strcmp(t->group, s->group))
                s->group_number = t->group_number;
        if (s->group_number)
            continue;
        struct nos_sym *y = nos_symbols;
        while (y && strcmp(y->name, s->group))
            y = y->next;
        if (!y || (!y->section && !y->absolute)) { /* グループを名づけるシンボルは、定義されたシンボル */
            nasm_nonfatal("セクション `%s' のグループのシンボル `%s' が%s", s->name, s->group,
                          y ? "定義されていない" : "ない");
            continue;
        }
        struct nos_group g = {.symbol = htole32(y->number), .rule = htole32(NOS_GROUP_FIRST)};
        saa_wbytes(groups, &g, sizeof g);
        s->group_number = ++group_count;
    }
    src_set(line, file);
}

/* SYMBOLS：番号 0 はファイル、続いて各セクションのシンボル、続いてラベル。 */
static void nos_symbols_write(struct SAA *symbols, struct SAA *strings)
{
    struct nos_symbol e;
    uint32_t number = 0;

    nasm_zero(e);
    e.name = htole32(nos_string(strings, inname));
    e.section = htole32(NOS_SYMBOL_ABSOLUTE);
    e.binding = NOS_BINDING_LOCAL;
    e.type = NOS_SYMBOL_FILE;
    e.visibility = NOS_VISIBILITY_IMAGE;
    saa_wbytes(symbols, &e, sizeof e);
    number++;

    for (struct nos_sect *s = nos_sections; s; s = s->next) {
        nasm_zero(e);
        e.section = htole32(s->index + 1);
        e.binding = NOS_BINDING_LOCAL;
        e.type = NOS_SYMBOL_SECTION;
        e.visibility = NOS_VISIBILITY_IMAGE;
        saa_wbytes(symbols, &e, sizeof e);
        number++;
    }

    for (struct nos_sym *y = nos_symbols; y; y = y->next) {
        y->number = number++;
        nasm_zero(e);
        e.name = htole32(nos_string(strings, y->name));
        e.section = htole32(y->section ? y->section->index + 1
                            : y->absolute ? NOS_SYMBOL_ABSOLUTE
                                          : NOS_SYMBOL_UNDEFINED);
        e.value = htole64(y->value);
        e.size = htole64(y->size);
        e.binding = y->binding;
        e.type = y->type;
        e.visibility = y->visibility;
        saa_wbytes(symbols, &e, sizeof e);
    }
}

/* RELOCATIONS：セクションの順に続けて並べる。セクションのシンボルの番号は、セクションの番号 + 1。 */
static void nos_relocations_write(struct SAA *relocations)
{
    uint64_t first = 0;

    for (struct nos_sect *s = nos_sections; s; s = s->next) {
        const struct nos_reloc *r;

        s->reloc_first = first;
        first += s->reloc_count;
        saa_rewind(s->relocs);
        while ((r = saa_rstruct(s->relocs))) {
            struct nos_relocation e = {
                .offset = htole64(r->offset),
                .addend = (int64_t)htole64((uint64_t)r->addend),
                .kind = htole32(r->kind),
                .symbol = htole32(r->section ? r->section->index + 1 : r->symbol->number),
            };
            saa_wbytes(relocations, &e, sizeof e);
        }
    }
}

static void nos_sections_write(struct SAA *sections)
{
    for (struct nos_sect *s = nos_sections; s; s = s->next) {
        struct nos_section e = {
            .name = htole32(s->name_offset),
            .role = htole32(s->role),
            .align_shift = htole32(s->align_shift),
            .flags = htole32(s->flags),
            .group = htole32(s->group_number),
            .parent = htole32(s->parent_number),
            .entry_bytes = htole32(s->entry_bytes),
            .priority = htole32(s->priority),
            .dwarf_part = htole32(s->dwarf_part),
            .offset = htole64(nos_role_empty(s->role) ? 0 : s->offset),
            .bytes = htole64(s->bytes),
            .relocation_first = htole64(s->reloc_first),
            .relocation_count = htole64(s->reloc_count),
        };
        saa_wbytes(sections, &e, sizeof e);
    }
}

static uint64_t nos_align8(uint64_t x)
{
    return (x + 7) & ~UINT64_C(7);
}

/* スライスの中の置き方：オブジェクトのヘッダー、テーブルの一覧、テーブルの中身、セクションの中身の順で、どれも 8 に
   揃う。 */
struct nos_layout {
    uint32_t count;
    uint32_t types[NOS_OBJECT_GROUPS];
    struct SAA *tables[NOS_OBJECT_GROUPS];
    uint64_t bytes[NOS_OBJECT_GROUPS];
    uint64_t offsets[NOS_OBJECT_GROUPS];
    uint64_t end;               /* スライスの長さ */
};

static void nos_layout_table(struct nos_layout *l, uint32_t type, struct SAA *table, uint64_t bytes)
{
    l->types[l->count] = type;
    l->tables[l->count] = table;
    l->bytes[l->count] = bytes;
    l->count++;
}

/* テーブルの中身と、セクションの中身（s->offset）の場所を決める。 */
static void nos_layout_place(struct nos_layout *l)
{
    uint64_t at = sizeof(struct nos_object) + l->count * sizeof(struct nos_table);

    for (uint32_t i = 0; i < l->count; i++) {
        l->offsets[i] = nos_align8(at);
        at = l->offsets[i] + l->bytes[i];
    }
    for (struct nos_sect *s = nos_sections; s; s = s->next) {
        if (nos_role_empty(s->role))
            continue;
        s->offset = nos_align8(at);
        at = s->offset + s->bytes;
    }
    l->end = nos_align8(at);
}

/* コンテナのヘッダー、スライスの配列（一つ）、スライスを書く。 */
static void nos_file_write(const struct nos_layout *l)
{
    static const uint8_t magic[8] = {NOS_MAGIC_BYTES};
    struct nos_table list[NOS_OBJECT_GROUPS];
    uint64_t at;

    struct nos_file file = {
        .format = htole32(NOS_FORMAT),
        .kind = htole32(NOS_KIND_OBJECT),
        .head_bytes = htole32(sizeof(struct nos_file)),
        .slice_count = htole32(1),
        .slice_bytes = htole32(sizeof(struct nos_slice)),
        .table_bytes = htole32(sizeof(struct nos_table)),
        .slices = htole64(sizeof(struct nos_file)),
    };
    struct nos_slice slice = {
        .machine = htole32(NOS_MACHINE_X86_64),
        .offset = htole64(sizeof(struct nos_file) + sizeof(struct nos_slice)),
        .bytes = htole64(l->end),
    };
    struct nos_object object = {
        .head_bytes = htole32(sizeof(struct nos_object)),
        .table_count = htole32(l->count),
        .table_bytes = htole32(sizeof(struct nos_table)),
        .tables = htole64(sizeof(struct nos_object)),
    };

    memcpy(file.magic, magic, sizeof magic);
    nasm_zero(list);
    for (uint32_t i = 0; i < l->count; i++) {
        bool strings = l->types[i] == NOS_OBJECT_STRINGS;

        list[i].type = htole32(l->types[i]);
        list[i].flags = htole32(strings ? 0 : NOS_TABLE_CRITICAL);
        list[i].entry_bytes = htole32(strings ? 0 : (uint32_t)l->tables[i]->elem_len);
        list[i].offset = htole64(l->offsets[i]);
        list[i].bytes = htole64(l->bytes[i]);
    }
    nasm_write(&file, sizeof file, ofile);
    nasm_write(&slice, sizeof slice, ofile);
    nasm_write(&object, sizeof object, ofile);
    nasm_write(list, l->count * sizeof(struct nos_table), ofile);

    at = sizeof(struct nos_object) + l->count * sizeof(struct nos_table);
    for (uint32_t i = 0; i < l->count; i++) {
        nasm_assert(l->tables[i]->datalen == l->bytes[i]);
        fwritezero(l->offsets[i] - at, ofile);
        saa_fpwrite(l->tables[i], ofile);
        at = l->offsets[i] + l->bytes[i];
    }
    for (struct nos_sect *s = nos_sections; s; s = s->next) {
        if (nos_role_empty(s->role))
            continue;
        fwritezero(s->offset - at, ofile);
        saa_fpwrite(s->data, ofile);
        at = s->offset + s->bytes;
    }
    fwritezero(l->end - at, ofile);
}

static void nos_write(void)
{
    struct SAA *strings = saa_init(1);
    struct SAA *sections = saa_init(sizeof(struct nos_section));
    struct SAA *symbols = saa_init(sizeof(struct nos_symbol));
    struct SAA *relocations = saa_init(sizeof(struct nos_relocation));
    struct SAA *groups = saa_init(sizeof(struct nos_group));
    struct nos_layout l;

    saa_write8(strings, 0); /* オフセット 0 は空の名前（セクションのシンボル） */
    for (struct nos_sect *s = nos_sections; s; s = s->next)
        s->name_offset = nos_string(strings, s->name);
    nos_symbols_write(symbols, strings);
    nos_relocations_write(relocations);
    nos_sections_resolve(groups);

    nasm_zero(l);
    nos_layout_table(&l, NOS_OBJECT_STRINGS, strings, strings->datalen);
    nos_layout_table(&l, NOS_OBJECT_SECTIONS, sections, (uint64_t)nos_section_count * sizeof(struct nos_section));
    nos_layout_table(&l, NOS_OBJECT_SYMBOLS, symbols, symbols->datalen);
    if (relocations->datalen)
        nos_layout_table(&l, NOS_OBJECT_RELOCATIONS, relocations, relocations->datalen);
    if (groups->datalen)
        nos_layout_table(&l, NOS_OBJECT_GROUPS, groups, groups->datalen);
    nos_layout_place(&l);
    nos_sections_write(sections);
    nos_file_write(&l);

    saa_free(strings);
    saa_free(sections);
    saa_free(symbols);
    saa_free(relocations);
    saa_free(groups);
}

static void nos_free(void)
{
    while (nos_sections) {
        struct nos_sect *s = nos_sections;

        nos_sections = s->next;
        nasm_free(s->name);
        nasm_free(s->group);
        nasm_free(s->parent);
        if (s->data)
            saa_free(s->data);
        if (s->rows)
            saa_free(s->rows);
        saa_free(s->relocs);
        nasm_free(s);
    }
    while (nos_symbols) {
        struct nos_sym *y = nos_symbols;

        nos_symbols = y->next;
        nasm_free(y->size_text);
        nasm_free(y);
    }
    hash_free(&nos_section_names);
    raa_free(nos_section_segments);
    raa_free(nos_undefined_segments);
}

static void nos_cleanup(void)
{
    dfmt->cleanup();
    nos_symbols_finish();
    nos_write();
    nos_free();
}

static void nos_init(void)
{
    nos_sections = NULL;
    nos_sections_tail = &nos_sections;
    nos_section_count = 0;
    nos_section_segments = raa_init();
    nos_default_segment = seg_alloc();
    nos_symbols = NULL;
    nos_symbols_tail = &nos_symbols;
    nos_undefined_segments = raa_init();
    nos_slot_segment = seg_alloc() + 1;
    backend_label("..slot", nos_slot_segment, 0);
}

extern macros_t nos_stdmac[];

static const struct dfmt nos_dwarf = {
    "DWARF 5 の行番号",
    "dwarf",
    nos_dwarf_init,
    nos_dwarf_linenum,
    null_debug_deflabel,
    NULL, /* .debug_smacros */
    NULL, /* .debug_include */
    NULL, /* .debug_mmacros */
    null_debug_directive,
    null_debug_typevalue,
    null_debug_output,
    nos_dwarf_cleanup,
    NULL /* pragma list */
};

static const struct dfmt *const nos_debug_formats[] = {&nos_dwarf, NULL};

const struct ofmt of_nos = {
    "Nocturne の .NOS のオブジェクト（x86-64）",
    "nos",
    ".o",
    OFMT_ZERODATA,
    64,
    nos_debug_formats,
    &nos_dwarf,
    nos_stdmac,
    nos_init,
    null_reset,
    nos_out,
    nos_symdef,
    nos_section,
    NULL, /* herelabel */
    nos_sectalign,
    null_segbase,
    null_directive,
    nos_cleanup,
    NULL /* pragmas */
};

#endif /* OF_NOS */
