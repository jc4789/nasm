// SPDX-FileCopyrightText: Nocturne contributors
//
// SPDX-License-Identifier: Apache-2.0

/* .NOS：Nocturne の実行ファイル形式。オブジェクトファイル、アーカイブ、プログラム、ライブラリー、ドライバー、カーネル
   のイメージは、どれもこの形式で、ヘッダーの kind で分ける。このヘッダーは形（構造体、値、最初の大きさ）を持ち、規則と
   理由（拡張の規則、置き方、選び方、ハッシュの範囲）は docs/nos.md にある。<stdint.h> だけを使い、ほかの言語のツール
   （LLVM のフォーク、Go のリンカー、object クレート）は、ここからコピーか生成で同じ定義を持つ。

   ファイル：struct nos_file（コンテナのヘッダー）、struct nos_slice の配列、コンテナのテーブルの一覧。スライスは
   一つのアーキテクチャのイメージかオブジェクトで、中のオフセットは全部スライスの先頭から数える。数値は little endian。 */
#ifndef NOCTURNE_NOS_H
#define NOCTURNE_NOS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- コンテナ ---- */

/* 値の理由は docs/nos.md の 2 章。 */
#define NOS_MAGIC_BYTES 0x89, 'N', 'O', 'S', 0x0D, 0x0A, 0x00, 0x0A
#define NOS_FORMAT 1u

enum nos_kind {
    NOS_KIND_OBJECT = 1,  /* コンパイラーとアセンブラーの出力。スライスは一つ */
    NOS_KIND_ARCHIVE = 2, /* オブジェクトとツール用のデータの集まり。スライスは一つ */
    NOS_KIND_PROGRAM = 3, /* プロセスの最初のイメージ */
    NOS_KIND_LIBRARY = 4, /* 動いているプロセスにマップするイメージ（SYS_IMAGE_MAP） */
    NOS_KIND_DRIVER = 5,  /* カーネルのドライバーのイメージ */
    NOS_KIND_KERNEL = 6,  /* カーネル。ブートローダーがマップする */
};

struct nos_file {
    uint8_t magic[8];      /* NOS_MAGIC_BYTES */
    uint32_t format;       /* NOS_FORMAT */
    uint32_t kind;         /* enum nos_kind */
    uint32_t head_bytes;   /* このヘッダーの大きさ */
    uint32_t slice_count;  /* 1 以上 */
    uint32_t slice_bytes;  /* struct nos_slice 一つの大きさ */
    uint32_t table_count;  /* コンテナのテーブル */
    uint32_t table_bytes;  /* struct nos_table 一つの大きさ */
    uint32_t reserved;
    uint64_t slices;       /* struct nos_slice の配列の、ファイル先頭からのオフセット（8 に揃う） */
    uint64_t tables;       /* コンテナのテーブルの一覧の、ファイル先頭からのオフセット（8 に揃う） */
};

enum nos_machine {
    NOS_MACHINE_X86_64 = 1,
};

struct nos_slice {
    uint32_t machine;  /* enum nos_machine */
    uint32_t reserved;
    uint64_t offset;   /* スライスの範囲の、ファイル先頭からのオフセット。イメージはページ境界に揃う */
    uint64_t bytes;    /* スライスの範囲の長さ */
    uint64_t head;     /* スライスのヘッダー（struct nos_image か struct nos_object）の、スライス先頭からのオフセット */
};

/* x86-64 の機能の番号（スライスの選び方は docs/nos.md の 2 章）：最低要件（common/x86_64/cpu_baseline.h。x86-64-v4 の命令を含む）を超える命令セットの
   拡張。CPUID の場所は leaf.subleaf:レジスター[bit] で、出典は Intel SDM 093 Vol. 1 第 21 章、Intel ISE 319433-063 の
   表 1-3、Intel APX 355828-009、Intel AVX10.2 361050-007、AMD PPR 57238 rev 0.49、AMD APM 24594 rev 3.34 付録 E。
   番号は増やすだけで、意味を変えない。 */
enum nos_x86_64_feature {
    NOS_X86_64_SSE4A = 1,              /* 0x80000001:ECX[6]（AMD） */
    NOS_X86_64_XSAVEOPT = 2,           /* 0xD.1:EAX[0] */
    NOS_X86_64_XSAVEC = 3,             /* 0xD.1:EAX[1] */
    NOS_X86_64_PCLMULQDQ = 4,          /* 1:ECX[1] */
    NOS_X86_64_AES = 5,                /* 1:ECX[25] */
    NOS_X86_64_VAES = 6,               /* 7.0:ECX[9] */
    NOS_X86_64_VPCLMULQDQ = 7,         /* 7.0:ECX[10] */
    NOS_X86_64_GFNI = 8,               /* 7.0:ECX[8] */
    NOS_X86_64_SHA = 9,                /* 7.0:EBX[29] */
    NOS_X86_64_SHA512 = 10,            /* 7.1:EAX[0] */
    NOS_X86_64_SM3 = 11,               /* 7.1:EAX[1] */
    NOS_X86_64_SM4 = 12,               /* 7.1:EAX[2] */
    NOS_X86_64_ADX = 13,               /* 7.0:EBX[19] */
    NOS_X86_64_RDRAND = 14,            /* 1:ECX[30] */
    NOS_X86_64_RDSEED = 15,            /* 7.0:EBX[18] */
    NOS_X86_64_PREFETCHW = 16,         /* 0x80000001:ECX[8] */
    NOS_X86_64_PKU = 17,               /* 7.0:ECX[3]。XCR0[9] */
    NOS_X86_64_CLWB = 18,              /* 7.0:EBX[24] */
    NOS_X86_64_RDPID = 19,             /* 7.0:ECX[22] */
    NOS_X86_64_RDPRU = 20,             /* 0x80000008:EBX[4]（AMD） */
    NOS_X86_64_MWAITX = 21,            /* 0x80000001:ECX[29]（AMD） */
    NOS_X86_64_CLZERO = 22,            /* 0x80000008:EBX[0]（AMD） */
    NOS_X86_64_CLDEMOTE = 23,          /* 7.0:ECX[25] */
    NOS_X86_64_WAITPKG = 24,           /* 7.0:ECX[5] */
    NOS_X86_64_MOVDIRI = 25,           /* 7.0:ECX[27] */
    NOS_X86_64_MOVDIR64B = 26,         /* 7.0:ECX[28] */
    NOS_X86_64_SERIALIZE = 27,         /* 7.0:EDX[14] */
    NOS_X86_64_CMPCCXADD = 28,         /* 7.1:EAX[7] */
    NOS_X86_64_RAO_INT = 29,           /* 7.1:EAX[3] */
    NOS_X86_64_PREFETCHI = 30,         /* 7.1:EDX[14]（Intel）か 0x80000021:EAX[20]（AMD） */
    NOS_X86_64_MOVRS = 31,             /* 7.1:EAX[31] */
    NOS_X86_64_AVX512_VBMI = 32,       /* 7.0:ECX[1] */
    NOS_X86_64_AVX512_VBMI2 = 33,      /* 7.0:ECX[6] */
    NOS_X86_64_AVX512_IFMA = 34,       /* 7.0:EBX[21] */
    NOS_X86_64_AVX512_VNNI = 35,       /* 7.0:ECX[11] */
    NOS_X86_64_AVX512_BITALG = 36,     /* 7.0:ECX[12] */
    NOS_X86_64_AVX512_VPOPCNTDQ = 37,  /* 7.0:ECX[14] */
    NOS_X86_64_AVX512_BF16 = 38,       /* 7.1:EAX[5] */
    NOS_X86_64_AVX512_FP16 = 39,       /* 7.0:EDX[23] */
    NOS_X86_64_AVX512_VP2INTERSECT = 40, /* 7.0:EDX[8] */
    NOS_X86_64_AVX_VNNI = 41,          /* 7.1:EAX[4] */
    NOS_X86_64_AVX_IFMA = 42,          /* 7.1:EAX[23] */
    NOS_X86_64_AVX_VNNI_INT8 = 43,     /* 7.1:EDX[4] */
    NOS_X86_64_AVX_VNNI_INT16 = 44,    /* 7.1:EDX[10] */
    NOS_X86_64_AVX_NE_CONVERT = 45,    /* 7.1:EDX[5] */
    NOS_X86_64_AMX_TILE = 46,          /* 7.0:EDX[24]。AMX の全部は XCR0[18:17] */
    NOS_X86_64_AMX_INT8 = 47,          /* 7.0:EDX[25] */
    NOS_X86_64_AMX_BF16 = 48,          /* 7.0:EDX[22] */
    NOS_X86_64_AMX_FP16 = 49,          /* 7.1:EAX[21] */
    NOS_X86_64_AMX_COMPLEX = 50,       /* 7.1:EDX[8] */
    NOS_X86_64_AMX_FP8 = 51,           /* 0x1E.1:EAX[4] */
    NOS_X86_64_AMX_MOVRS = 52,         /* 0x1E.1:EAX[8] */
    NOS_X86_64_AMX_AVX512 = 53,        /* 0x1E.1:EAX[7] */
    NOS_X86_64_AVX10_1 = 54,           /* 7.1:EDX[19] で、0x24.0:EBX[7:0] が 1 以上 */
    NOS_X86_64_AVX10_2 = 55,           /* 7.1:EDX[19] で、0x24.0:EBX[7:0] が 2 以上 */
    NOS_X86_64_APX_F = 56,             /* 7.1:EDX[21]。XCR0[19] */
    NOS_X86_64_APX_NCI_NDD_NF = 57,    /* 0x29.0:EBX[0]（APX_F の時だけ leaf 0x29 がある） */
};
/* 機能の番号の上限（一番大きい番号 + 1）。番号を足したら、これも変える。機能の bit の配列の大きさに使う。 */
#define NOS_X86_64_FEATURE_COUNT 58u

/* ---- テーブル ---- */

/* テーブルの中身は、ファイルとスライスの中の位置を持たない：ほかのテーブルを指す時は、そのテーブルの先頭から数える。
   位置を持つのはヘッダー、スライスの配列、リージョンの記述、テーブルの一覧だけなので、道具は知らない種類のテーブルも
   中身を変えずに動かせる。 */
#define NOS_TABLE_CRITICAL 0x1u /* 知らない種類なら、読み込み側はそのイメージを拒否する */
#define NOS_TABLE_DETACHED 0x2u /* 外してもイメージの意味が変わらない。ハッシュに含めない。nos strip が外せる */

struct nos_table {
    uint32_t type;         /* コンテナとスライスでそれぞれの enum */
    uint32_t flags;        /* NOS_TABLE_* */
    uint32_t entry_bytes;  /* エントリーの大きさ。0 はエントリーに分けない byte 列 */
    uint32_t reserved;
    uint64_t offset;       /* コンテナのテーブルはファイル先頭から、スライスのテーブルはスライス先頭から。8 に揃う */
    uint64_t bytes;        /* entry_bytes が 0 でなければ、その倍数 */
};

/* コンテナのテーブル：アーキテクチャに依存しないもの。fat binary のスライスはこれを共有する。 */
enum nos_file_table {
    NOS_FILE_STRINGS = 1,       /* byte 列：NUL 終端の UTF-8。ほかのコンテナのテーブルがオフセットで指す */
    /* 2〜8 は空き。プログラムの宣言（起動の入口、表示名、開ける文書の種類、スクリプト、アイコン）はイメージに置かず、
       パッケージの宣言にある（docs/records/package-20261127/sekkei.md の 2.2） */
    /* CRITICAL。struct nos_driver_manifest。一つ。C:\Nocturne\Drivers の装置プログラムの種別と要求能力。 */
    NOS_FILE_DRIVER_MANIFEST = 9,
};

/* ---- 装置プログラムのマニフェスト（NOS_FILE_DRIVER_MANIFEST） ----
   C:\Nocturne\Drivers の装置プログラム（RULES を持つ NOS_KIND_PROGRAM）が自分の役割と必要能力を宣言する。
   カーネルの装置の管理者が読み込み時に種別と能力ホワイトリストを検証し、違反する像は拒否する。 */
#define NOS_DRIVER_ROLE_DISPLAY 0x1u /* 表示出力（SYS_DISPLAY_DRIVER_*）を提供する */
#define NOS_DRIVER_ROLE_GPU 0x2u     /* GPU adapter（SYS_GPU_ADAPTER_*）を提供する */
#define NOS_DRIVER_ROLES_KNOWN (NOS_DRIVER_ROLE_DISPLAY | NOS_DRIVER_ROLE_GPU)
struct nos_driver_manifest {
    uint32_t roles;        /* NOS_DRIVER_ROLE_* の組 */
    uint32_t flags;        /* 0 */
    uint64_t capabilities; /* (1ull << enum n_cap) の組（0 は不正） */
};

/* ---- イメージのスライス（program、library、driver、kernel） ---- */

enum nos_hash {
    NOS_HASH_SHA256 = 1, /* hash の先頭 32 byte */
};

struct nos_range {
    uint64_t address; /* ベースからのオフセット */
    uint64_t bytes;
};

struct nos_image {
    uint32_t head_bytes;     /* このヘッダーの大きさ */
    uint32_t page_shift;     /* リージョンのアラインメント。x86-64 は 12 */
    uint32_t region_count;
    uint32_t region_bytes;   /* struct nos_region 一つの大きさ */
    uint32_t table_count;
    uint32_t table_bytes;    /* struct nos_table 一つの大きさ */
    uint32_t feature_count;  /* 要求する機能の数（最低要件を超える分） */
    uint32_t hash_kind;      /* enum nos_hash */
    uint8_t hash[64];        /* イメージのハッシュ（下の「ハッシュ」） */
    uint64_t features;       /* uint16_t[feature_count] の、スライス先頭からのオフセット。昇順で重複しない */
    uint64_t regions;        /* struct nos_region[region_count] の、スライス先頭からのオフセット（8 に揃う） */
    uint64_t tables;         /* struct nos_table[table_count] の、スライス先頭からのオフセット（8 に揃う） */
    uint64_t align;          /* ベースアドレスのアラインメント。ページ以上の 2 の冪 */
    uint64_t span;           /* ベースアドレスからのアドレス範囲の長さ。ページの倍数 */
    uint64_t entry;          /* エントリーポイントの、ベースからのオフセット。CODE の中。PROGRAM、DRIVER、KERNEL と、
                                NOS_PROVIDER_PROCESS を提供する LIBRARY だけが使い、ほかの LIBRARY は 0 */
    struct nos_range init;   /* 8 byte の関数アドレスの配列。前から呼ぶ。RELOCATED の中 */
    struct nos_range fini;   /* 後ろから呼ぶ。RELOCATED の中 */
    struct nos_range tls;    /* TLS テンプレート（初期値の部分）。CONSTANT か RELOCATED の中 */
    uint64_t tls_bytes;      /* TLS ブロック全体（テンプレートと、0 で埋める残り）。0 は TLS なし */
    uint64_t tls_align;      /* 2 の冪。TLS がなければ 0 */
    struct nos_range unwind_index; /* struct nos_unwind[]、start の昇順。CONSTANT の中 */
    struct nos_range unwind;       /* CFI の CIE と FDE（docs/nocturne-native-abi.md §0）。CONSTANT の中 */
};

/* init、fini、tls、unwind_index、unwind は、マップしたイメージの中のアドレスで、プロセスの中のコードが読む。カーネルは
   これを struct n_image（common/abi.h）に入れて、起動の集合は struct n_start で、後から読み込んだイメージは
   SYS_IMAGE_MAP の結果で中核のライブラリーに渡す。ドライバーとカーネルは init、fini、tls、アンワインドのテーブルを持たない。 */

struct nos_unwind {
    uint32_t start; /* 関数の先頭の、ベースからのオフセット */
    uint32_t fde;   /* その FDE の、ベースからのオフセット */
};

enum nos_region_kind {
    NOS_REGION_CODE = 1,      /* 読み取りと実行。全プロセスで同じ物理ページ。リロケーションなし */
    NOS_REGION_CONSTANT = 2,  /* 読み取り。全プロセスで同じ物理ページ。リロケーションなし */
    NOS_REGION_RELOCATED = 3, /* ベースごとにリロケーションを適用した後、読み取り専用。アクセスのあったページだけが
                                 プロセスのプライベートなページになる */
    NOS_REGION_DATA = 4,      /* 読み書き。プロセスのプライベートなページ */
};

struct nos_region {
    uint32_t kind;          /* enum nos_region_kind */
    uint32_t flags;         /* 0（拡張の規則の flags） */
    uint64_t address;       /* ベースからのオフセット。ページ境界 */
    uint64_t memory_bytes;  /* 0 より大きい */
    uint64_t file_offset;   /* スライス先頭からのオフセット。ページ境界 */
    uint64_t file_bytes;    /* CODE、CONSTANT、RELOCATED は memory_bytes と同じ。DATA は以下で、残りは 0 */
};
/* リージョンはアドレスの昇順で、ページを分け合わず、[0, span) の中にある。間の隙間はマップしない。ファイルの中の
   データもページを分け合わず、最後のページの終わりまでスライスの中にあり、file_bytes の後ろからページの終わりまでは
   0（ほかのものを置かない）。そのため、ファイルのページをそのまま写したページの中身は、全部イメージのハッシュで決まる。
   CODE と CONSTANT はリロケーションを持たないので、リンカーは、イメージのアドレスの絶対値が要る CODE のセクションを
   エラーにし、CONSTANT のセクションを RELOCATED に置く。 */

/* スライスのテーブル。 */
enum nos_image_table {
    NOS_IMAGE_STRINGS = 1,          /* byte 列：NUL 終端の UTF-8。ほかのテーブルがオフセットで指す */
    NOS_IMAGE_RELOCATIONS = 2,      /* CRITICAL。struct nos_relocation_run */
    NOS_IMAGE_RELOCATION_DATA = 3,  /* CRITICAL。byte 列：ページの索引とページ内のオフセット */
    NOS_IMAGE_REQUIRES = 4,         /* CRITICAL。struct nos_provider：要求するプロバイダー */
    NOS_IMAGE_IMPORTS = 5,          /* CRITICAL。struct nos_import */
    NOS_IMAGE_PROVIDES = 6,         /* struct nos_provider：自分が提供するプロバイダー */
    NOS_IMAGE_EXPORTS = 7,          /* struct nos_export。(provider, 名前の byte) の昇順 */
    NOS_IMAGE_DRIVER = 8,           /* struct nos_driver。一つ */
    NOS_IMAGE_RULES = 9,            /* struct n_hal_term（common/hal_abi.h）の並び。NOS_RULE_TERM_BYTES */
    NOS_IMAGE_KERNEL = 10,          /* CRITICAL。struct nos_kernel。ページテーブルの段数ごとに一つ */
    NOS_IMAGE_NAMES = 11,           /* DETACHED。struct nos_name。start の昇順 */
    NOS_IMAGE_NAME_STRINGS = 12,    /* DETACHED。byte 列：NAMES の名前 */
    NOS_IMAGE_BLOBS = 13,           /* struct nos_blob：ツール用の、マップしないデータ（rustc のメタデータ） */
    NOS_IMAGE_BLOB_DATA = 14,       /* byte 列：BLOBS の中身 */
    NOS_IMAGE_FORWARDS = 15,        /* CRITICAL。struct nos_forward：転送のエクスポート。(provider, 名前の byte) の昇順 */
    NOS_IMAGE_REPLACEMENTS = 16,    /* CRITICAL。struct nos_replacement。PROGRAM だけ。(provider, 名前の byte) の昇順 */
    NOS_IMAGE_TLS_ARGUMENTS = 17,   /* CRITICAL。struct nos_tls_argument。LIBRARY だけ。slot の昇順で重ならない */
    NOS_IMAGE_STACK_SIZES = 18,     /* DETACHED。struct nos_stack_size。address の昇順で重ならない */
    /* DWARF（DETACHED、byte 列）：NOS_IMAGE_DWARF + enum nos_dwarf。オブジェクトの DEBUG のセクションを、部分ごとに
       並べたもの */
    NOS_IMAGE_DWARF = 0x100,
};

/* DWARF の部分（DWARF 5 の 7.1 の名前から .debug_ を除いたもの）。 */
enum nos_dwarf {
    NOS_DWARF_INFO = 1,
    NOS_DWARF_ABBREV = 2,
    NOS_DWARF_LINE = 3,
    NOS_DWARF_LINE_STR = 4,
    NOS_DWARF_STR = 5,
    NOS_DWARF_STR_OFFSETS = 6,
    NOS_DWARF_ADDR = 7,
    NOS_DWARF_RNGLISTS = 8,
    NOS_DWARF_LOCLISTS = 9,
    NOS_DWARF_ARANGES = 10,
    NOS_DWARF_NAMES = 11,
    NOS_DWARF_FRAME = 12,
    NOS_DWARF_MACRO = 13,
    NOS_DWARF_RANGES = 14,   /* DWARF 4 */
    NOS_DWARF_LOC = 15,      /* DWARF 4 */
    NOS_DWARF_TYPES = 16,    /* DWARF 4 */
    NOS_DWARF_PUBNAMES = 17, /* DWARF 4 */
    NOS_DWARF_PUBTYPES = 18, /* DWARF 4 */
};

/* ---- ベースリロケーション ----
   イメージはベース 0 でリンクする。ファイルの中の値はベース 0 での値で、マップする側がベースを足す。

   RELOCATIONS のエントリーは、リージョン一つの分。ページ i のリロケーションは、RELOCATION_DATA の
   uint32_t start[pages + 1]（run.pages の位置）と uint16_t offset[start[pages]]（run.entries の位置）で、
   offset[start[i]] から offset[start[i + 1] - 1] まで。値はページ内の byte オフセットの昇順で重複せず、8 byte の値は
   ページ境界をまたがない（またぐ値はリンカーのエラー）。pages はリージョンの file_bytes をページに切り上げた数。

   プログラムとライブラリーでは、カーネルがページに初めてアクセスがあった時に、そのページの内容を読み、そのページの分
   だけを適用してマップする。RELOCATED のページは読み取り専用でマップする。ドライバーとカーネルは、読み込みの時に全部を
   適用する。 */
enum nos_relocate {
    NOS_RELOCATE_ADD64 = 1, /* その 8 byte の値にベースを足す。page_shift は 16 以下 */
};
struct nos_relocation_run {
    uint32_t region;   /* リージョンの番号（RELOCATED か DATA）。一つのリージョンに run は一つ */
    uint32_t kind;     /* enum nos_relocate */
    uint64_t pages;    /* RELOCATION_DATA の中の start[] のオフセット（4 に揃う） */
    uint64_t entries;  /* RELOCATION_DATA の中の offset[] のオフセット（2 に揃う） */
};

/* ---- プロバイダー、インポート、エクスポート ----
   プロバイダーは、名前とバージョン (major, minor) を持つ API の集まり。要求は「名前が同じで、major が同じで、minor が
   提供側の minor 以下」で満たされる。major 0（安定を約束しない版）の要求は、組んだ時の版 built が提供側の minor と同じ時
   だけ満たされる。カーネルが提供するのは nocturne.abi（エントリーイメージ。major が N_ABI_GENERATION、minor が 0）と
   nocturne.ddk（DDK_MAJOR、DDK_MINOR）。nocturne.abi を要求してよいのは、OS の印を持つ結び付けの表の項のライブラリーと、
   全部の呼び出しを持つ OS のプログラムで、ドライバーは nocturne.ddk だけを要求する。

   持てる種類：REQUIRES はどれも。IMPORTS は PROGRAM、LIBRARY、DRIVER。PROVIDES、EXPORTS、FORWARDS は PROGRAM、LIBRARY、
   KERNEL。REQUIRES と PROVIDES は名前の byte の昇順で、名前が重ならない。 */
#define NOS_PROVIDER_ABI "nocturne.abi"
#define NOS_PROVIDER_DDK "nocturne.ddk"

/* プロバイダーの旗（PROVIDES の flags。REQUIRES では 0）。一つのイメージの PROVIDES は、全部 SINGLE か、全部 SINGLE で
   ないかのどちらか。 */
#define NOS_PROVIDER_PROCESS 0x1u /* プロセスのランタイム。LIBRARY だけで、SINGLE も立つ。イメージに一つまで */
#define NOS_PROVIDER_SINGLE 0x2u  /* プロセスに一つ（名前と major の組ごとに、プロセスの中の提供者は一つ）。LIBRARY だけ */

struct nos_provider {
    uint32_t name;     /* STRINGS の中のオフセット */
    uint32_t major;
    uint32_t minor;    /* REQUIRES では読み込みに要る版（強いインポートの since の最大） */
    uint32_t flags;    /* NOS_PROVIDER_* */
    uint32_t built;    /* REQUIRES では組んだ時の版（リンクの入力の PROVIDES の minor）。PROVIDES では 0 */
    uint32_t reserved;
};

/* マップする側は、プロバイダーのエクスポート（無ければ転送）から名前を引き、slot の 8 byte にアドレスと addend の和を
   書く。slot は RELOCATED か DATA の中にある：リンカーが作るスロットとサンクの行き先は RELOCATED で、書いた後は読み取り
   専用になる。DATA の slot は、書き換えられるデータの中のポインターの初期値が、ほかのイメージのシンボルを指す所。
   IMPORTS は slot の昇順で、slot が重ならない。 */
#define NOS_IMPORT_WEAK 0x1u /* 弱い：要求の minor を上げず、エクスポートが無ければ slot に 0 を書く */
#define NOS_IMPORT_OWN 0x2u  /* provider は自分の PROVIDES の番号（SINGLE のプロバイダー）で、名前は自分の置き換えられる
                                エクスポート（NOS_EXPORT_REPLACEABLE）。プログラムが置き換えていれば、その定義に結ぶ */
struct nos_import {
    uint64_t slot;      /* ベースからのオフセット。8 に揃う */
    int64_t addend;
    uint32_t provider;  /* REQUIRES の番号（NOS_IMPORT_OWN なら PROVIDES の番号） */
    uint32_t name;      /* STRINGS の中のオフセット */
    uint32_t flags;     /* NOS_IMPORT_* */
    uint32_t reserved;
};

/* エクスポートの旗。どちらも番地の意味を変えない。 */
#define NOS_EXPORT_REPLACEABLE 0x1u /* 言語の標準がプログラムによる置き換えを決めている関数（C++ の operator new と
                                       operator delete）。SINGLE のプロバイダーのエクスポートだけが持てる */
#define NOS_EXPORT_EXCLUSIVE 0x2u   /* 同じ名前の定義がほかのイメージにあるとプロセスの状態が分かれる（malloc の一族）。
                                       リンカーは、オブジェクトかアーカイブがこの名前を定義すればエラーにする */
struct nos_export {
    uint64_t address;   /* ベースからのオフセット */
    uint32_t provider;  /* PROVIDES の番号 */
    uint32_t name;      /* STRINGS の中のオフセット */
    uint32_t since;     /* このシンボルが現れた、プロバイダーの minor（その minor 以下） */
    uint32_t flags;     /* NOS_EXPORT_* */
};

/* 転送のエクスポート：自分のプロバイダー provider の名前 name を、自分の要求 target のプロバイダーの同じ名前の
   エクスポートとして提供する。マップする側は行き先までたどり、行き先の実体の番地を slot に書く。同じ provider の
   EXPORTS と同じ名前を持たない。 */
struct nos_forward {
    uint32_t provider;  /* PROVIDES の番号 */
    uint32_t name;      /* STRINGS の中のオフセット */
    uint32_t target;    /* REQUIRES の番号 */
    uint32_t since;     /* 名前が provider に現れた minor（その minor 以下） */
};

/* 置き換え：プログラムが、要求 provider のプロバイダーの置き換えられるエクスポート name を、自分の定義 address で
   置き換える。マップする側は、プロセスの中で、転送をたどった行き先がそのエクスポートになるインポートを、全部 address に
   結ぶ。 */
struct nos_replacement {
    uint64_t address;   /* ベースからのオフセット。CODE の中 */
    uint32_t provider;  /* REQUIRES の番号 */
    uint32_t name;      /* STRINGS の中のオフセット */
};

/* ライブラリーの TLS のディスクリプターの 2 語目（下の「x86-64 のリロケーション」）：マップする側が、ページに初めて
   アクセスがあった時に、イメージの TLS の配置から書く。静的な TLS のイメージには S - TP（0 以下）、動的な TLS の
   イメージには「番号 << 32 | offset」（番号は 1 以上）。 */
struct nos_tls_argument {
    uint64_t slot;      /* ベースからのオフセット。RELOCATED の中で 8 に揃う */
    uint64_t offset;    /* イメージの TLS ブロックの中のオフセット。tls_bytes より小さく、2^32 より小さい */
};

/* 関数の固定のフレームの大きさ：コンパイラーが関数ごとに出した記録（オブジェクトの STACK_SIZES のセクション）を、
   リンカーが残った関数の分だけ並べたもの。契約の生成器の verify が、関数が使うスタックの上限を数えるのに読む
   （docs/contract.md）。可変の大きさのフレームを持つ関数とアセンブリーの関数は記録を持たない。 */
struct nos_stack_size {
    uint64_t address;   /* 関数の先頭の、ベースからのオフセット。CODE の中 */
    uint64_t bytes;     /* 関数がスタックに取る大きさ（退避するレジスターを含み、呼ばれた時の戻りの番地を含まない） */
};

/* ---- ドライバーとデバイスのマッチングルール ---- */
#define NOS_DRIVER_BOOT 0x1u /* ブートデバイスを読むまでの経路に要る：ブートローダーが読み込んで渡す */
struct nos_driver {
    uint32_t flags;  /* NOS_DRIVER_* */
    uint32_t name;   /* STRINGS の中のオフセット。デバイスマネージャーに見せるドライバーの名前 */
};

/* RULES のエントリーは 24 byte の項で、struct n_hal_term（common/hal_abi.h。項の意味はそこ）と同じ形：uint32_t key、
   uint32_t size、16 byte。size が 0 の項は数の項で、16 byte は uint64_t value と uint64_t mask。size が 1 から 16 の項は
   バイトの項で、16 byte の先頭 size byte が並び。キーが NOS_RULE_START の項がルールを始め、その value が続く項の数
   （1 以上）。 */
#define NOS_RULE_TERM_BYTES 24u
#define NOS_RULE_TERM_DATA 16u
#define NOS_RULE_START 0u

/* ---- カーネル ----
   KERNEL のエントリーは、カーネルが動けるページテーブルの段数ごとに一つで、リンカーがカーネルの宣言 nos.kernel
   （NOS_KERNEL）から作る。範囲は両方とも、その段数の線形アドレスの上半分（正準の番地）の中にある。ブートローダーが
   段数を決め、image の範囲から乱数でベースを選び（align に揃える）、ダイレクトマップの全体を direct の範囲に置く
   （common/boot_info.h）。 */
struct nos_kernel {
    uint32_t levels;                  /* ページテーブルの段数：4 か 5 */
    uint32_t flags;                   /* 0 */
    uint64_t image_low, image_high;   /* カーネルのイメージを置いてよい仮想アドレスの範囲 [low, high) */
    uint64_t direct_low, direct_high; /* ダイレクトマップの全体を置いてよい仮想アドレスの範囲 [low, high) */
    uint32_t boot_flags;              /* このカーネルが分かる、struct boot_info の flags の bit 0〜15。ローダーは、ここに無い
                                         bit 0〜15 を立てる引き渡しで、このカーネルに入らない */
    uint32_t reserved;
};

/* ---- 関数名とツール用のデータ ---- */
struct nos_name {
    uint64_t start;  /* ベースからのオフセット */
    uint64_t bytes;
    uint32_t name;   /* NAME_STRINGS の中のオフセット。コンパイラーが出したままの名前 */
    uint32_t flags;  /* 0 */
};

struct nos_blob {
    uint32_t name;    /* STRINGS の中のオフセット */
    uint32_t flags;   /* 0 */
    uint64_t offset;  /* BLOB_DATA の中のオフセット */
    uint64_t bytes;
};

/* ---- ハッシュ ----
   イメージのハッシュ（struct nos_image の hash）の範囲と並びは docs/nos.md の 5.9 で、計算は common/nos_hash.c。 */

/* ---- オブジェクトのスライス ---- */
struct nos_object {
    uint32_t head_bytes;     /* このヘッダーの大きさ */
    uint32_t table_count;
    uint32_t table_bytes;    /* struct nos_table 一つの大きさ */
    uint32_t feature_count;  /* コードが要求する機能（最低要件を超える分）。リンカーはイメージで和をとる */
    uint64_t features;       /* uint16_t[feature_count] の、スライス先頭からのオフセット。昇順で重複しない */
    uint64_t tables;         /* struct nos_table[table_count] の、スライス先頭からのオフセット（8 に揃う） */
};

enum nos_object_table {
    NOS_OBJECT_STRINGS = 1,      /* byte 列：NUL 終端の名前 */
    NOS_OBJECT_SECTIONS = 2,     /* CRITICAL。struct nos_section */
    NOS_OBJECT_SYMBOLS = 3,      /* CRITICAL。struct nos_symbol */
    NOS_OBJECT_RELOCATIONS = 4,  /* CRITICAL。struct nos_relocation。セクションごとに続けて並ぶ */
    NOS_OBJECT_GROUPS = 5,       /* CRITICAL。struct nos_group */
    NOS_OBJECT_CONTRACT_SYMBOLS = 6, /* struct nos_contract_symbol。symbol の昇順で重ならない */
};

/* セクションの役割。役割がリージョンを決め、名前は決めない（DECLARATION は名前で中身が決まる）。 */
enum nos_role {
    NOS_ROLE_CODE = 1,        /* CODE */
    NOS_ROLE_CONSTANT = 2,    /* CONSTANT（イメージのアドレスへのリロケーションを持てば、リンカーが RELOCATED に置く） */
    NOS_ROLE_DATA = 3,        /* DATA */
    NOS_ROLE_ZERO = 4,        /* DATA の 0 で埋める部分。ファイルに中身を持たない */
    NOS_ROLE_TLS_DATA = 5,    /* TLS テンプレートの初期値 */
    NOS_ROLE_TLS_ZERO = 6,    /* TLS の 0 で埋める部分。ファイルに中身を持たない */
    NOS_ROLE_INIT = 7,        /* 8 byte の関数アドレスの配列。priority の昇順、同じなら入力の順に並ぶ */
    NOS_ROLE_FINI = 8,        /* 同じ */
    NOS_ROLE_UNWIND = 9,      /* CFI の CIE と FDE */
    NOS_ROLE_DEBUG = 10,      /* DWARF の一つの部分（dwarf_part） */
    NOS_ROLE_DECLARATION = 11, /* 宣言（下の「宣言」）。名前は nos. で始まる。マップしない */
    NOS_ROLE_BLOB = 12,       /* ツール用のデータ。名前が BLOBS の名前になる。マップしない */
    NOS_ROLE_STACK_SIZES = 13, /* 関数の固定のフレームの大きさの記録（下）。マップしない */
};
/* DECLARATION と BLOB のセクションはアドレスを持たないので、その中のリロケーションと、その中のシンボルを指す
   リロケーションは、リンカーのエラー。

   STACK_SIZES のセクションの中身は、関数ごとに「関数の先頭の番地（8 byte。ABS64 のリロケーションで関数を指す）、
   フレームの大きさ（ULEB128。struct nos_stack_size の bytes）」の並びで、リロケーションは ABS64 だけ。parent は関数の
   セクションで、関数が残る時だけ残る。リンカーは、残った記録をイメージの STACK_SIZES のテーブルにする。 */

#define NOS_SECTION_RETAIN 0x1u /* 参照がなくても、不要セクションの削除で外さない */
#define NOS_SECTION_MERGE 0x2u  /* entry_bytes の要素の並びで、同じ要素をまとめてよい */
#define NOS_SECTION_STRINGS 0x4u /* MERGE で、要素が NUL 終端の文字列 */
/* MERGE のセクションを指すリロケーション：シンボルが SECTION なら、値 + A の位置の要素を指す。ほかのシンボルなら、
   そのシンボルの要素を指し、A はその要素からの差。書く側は、A が 0 でない参照のシンボルを SECTION のシンボルに
   替えない（替えると、別の要素を指しうる）。 */

#define NOS_PRIORITY_DEFAULT 65535u /* ソースに順序の指定が無い INIT と FINI の priority */

struct nos_section {
    uint32_t name;          /* STRINGS の中のオフセット */
    uint32_t role;          /* enum nos_role */
    uint32_t align_shift;   /* アラインメントは 1 << align_shift。30 以下 */
    uint32_t flags;         /* NOS_SECTION_* */
    uint32_t group;         /* GROUPS の番号 + 1。0 はなし */
    uint32_t parent;        /* セクションの番号 + 1：親が残る時だけ残る（関数の FDE など）。0 はなし */
    uint32_t entry_bytes;   /* MERGE の要素の大きさ */
    uint32_t priority;      /* INIT と FINI の順序。ソースに指定が無ければ NOS_PRIORITY_DEFAULT */
    uint32_t dwarf_part;    /* DEBUG の enum nos_dwarf */
    uint32_t reserved;
    uint64_t offset;        /* 中身の、スライス先頭からのオフセット。ZERO と TLS_ZERO は 0 */
    uint64_t bytes;         /* 大きさ */
    uint64_t relocation_first; /* RELOCATIONS の中の、このセクションの最初の番号 */
    uint64_t relocation_count;
};

/* 名前付きの配列：CODE、CONSTANT、DATA、ZERO のセクションで、名前が C の識別子のものは、同じ名前のセクションを続けて
   置き、リンカーが __section_first_<名前> と __section_end_<名前> を定義する。そのシンボルを参照するイメージでは、
   配列のセクションを削除しない。 */

#define NOS_SYMBOL_UNDEFINED 0u
#define NOS_SYMBOL_ABSOLUTE 0xFFFFFFFFu

enum nos_binding {
    NOS_BINDING_LOCAL = 1,
    NOS_BINDING_GLOBAL = 2,
    NOS_BINDING_WEAK = 3,
};
enum nos_symbol_type {
    NOS_SYMBOL_NONE = 0,
    NOS_SYMBOL_FUNCTION = 1,
    NOS_SYMBOL_DATA = 2,
    NOS_SYMBOL_TLS = 3,      /* TLS_DATA か TLS_ZERO のセクションの中 */
    NOS_SYMBOL_SECTION = 4,  /* セクションの先頭。name は 0 でよい */
    NOS_SYMBOL_FILE = 5,     /* 元のファイルの名前。section は NOS_SYMBOL_ABSOLUTE */
};
/* 見え方は、定義された GLOBAL と WEAK のシンボルだけが意味を持つ。LOCAL のシンボルには IMAGE を、未定義の
   シンボルには EXPORTABLE を書き、読み込み側はどちらも読まない。 */
enum nos_visibility {
    NOS_VISIBILITY_IMAGE = 1,      /* イメージの中だけ。エクスポートにならない */
    NOS_VISIBILITY_EXPORTABLE = 2, /* リンクの指定でエクスポートにできる */
};

struct nos_symbol {
    uint32_t name;         /* STRINGS の中のオフセット */
    uint32_t section;      /* セクションの番号 + 1。NOS_SYMBOL_UNDEFINED、NOS_SYMBOL_ABSOLUTE */
    uint64_t value;        /* セクションの中のオフセット。ABSOLUTE は値そのもの */
    uint64_t size;
    uint8_t binding;       /* enum nos_binding */
    uint8_t type;          /* enum nos_symbol_type */
    uint8_t visibility;    /* enum nos_visibility */
    uint8_t flags;         /* 0 */
    uint32_t reserved;
};

/* 契約の宣言への参照：コンパイラーが、契約の属性（docs/contract.md の nocturne_contract）を持つ宣言への未定義の
   シンボルに付ける。リンカーは、そのシンボルをリンクの入力のイメージから解決する時に、provider（major）のエクスポートに
   限り、エクスポートの since が since と違えばエラーにする。オブジェクトとアーカイブの定義で解決する時は読まない。
   読まない読み手が無視しても、名前による解決で正しいイメージになる（CRITICAL でない）。 */
struct nos_contract_symbol {
    uint32_t symbol;    /* SYMBOLS の番号。未定義のシンボル */
    uint32_t provider;  /* STRINGS の中のオフセット：プロバイダーの名前 */
    uint32_t major;
    uint32_t since;     /* 宣言が現れた minor */
};

struct nos_relocation {
    uint64_t offset;       /* セクションの中のオフセット */
    int64_t addend;
    uint32_t kind;         /* アーキテクチャごとの enum（x86-64 は enum nos_x86_64_relocation） */
    uint32_t symbol;       /* SYMBOLS の番号 */
};

enum nos_group_rule {
    NOS_GROUP_FIRST = 1, /* 同じグループは、リンカーが最初に読んだ一つだけを残す。名づけるシンボルが GLOBAL か WEAK
                            なら、名前が同じグループを同じとみなす。LOCAL なら、そのファイルの中だけで比べる（部分リンクで
                            LOCAL にしたグループは、ほかのファイルと競わない） */
};
struct nos_group {
    uint32_t symbol; /* グループを名づけるシンボルの番号。定義されたシンボル */
    uint32_t rule;   /* enum nos_group_rule */
};

/* x86-64 のリロケーション。S はシンボルのアドレス、A は addend、P はリロケーションの場所、G はリンカーが作る 8 byte の
   スロットのアドレス（中身は S）、TP はスレッドポインター（FS のベース）、D はリンカーが作る TLS のディスクリプター
   のアドレス。

   TLS のディスクリプターは 16 byte：解決の関数のアドレスと、その引数。解決の関数は NOS_TLS_RESOLVER の関数で、
   rax にディスクリプターのアドレスを受け、rax に S - TP を返す。rax とフラグのほかは、レジスターも状態も変えない
   （呼ぶ側は何も退避しない）。引数の 2 語目が TLS_ARGUMENTS の項なら、マップする側がイメージの TLS の配置から書く
   （struct nos_tls_argument）。項でなければ、2 語目は S の TLS ブロック内のオフセットで、解決の関数はライブラリー自身が
   定義し、TLS ブロックはスレッドが初めて使った時に作る。解決の関数は自分のイメージの TLS ブロックだけを扱うので、
   TLS のシンボルはエクスポートにならず、インポートもできない（リンカーのエラー）。ほかのイメージには、そのイメージの
   関数を通して渡す。

   プログラムの TLS ブロックは [TP - T, TP) にある。T は tls_bytes を tls_align の倍数に切り上げた値で、TP（スレッドの
   区画の先頭。%fs:0 がその番地を持つ）は tls_align に揃う。位置がリンクの時に決まるので、リンカーはプログラムの
   ディスクリプターを作らず、命令を固定オフセットに書き換える。 */
#define NOS_TLS_RESOLVER "__nos_tls_resolve"
enum nos_x86_64_relocation {
    NOS_X86_64_ABS64 = 1,        /* S + A。データの中のポインター */
    NOS_X86_64_ABS32 = 2,        /* S + A を 32 bit の符号なしで。絶対シンボルだけ */
    NOS_X86_64_ABS32S = 3,       /* S + A を 32 bit の符号付きで。絶対シンボルだけ */
    NOS_X86_64_PC32 = 4,         /* S + A - P。RIP 相対の参照、テーブルの差分 */
    NOS_X86_64_PC64 = 5,         /* S + A - P */
    NOS_X86_64_BRANCH32 = 6,     /* S + A - P。call と jmp、データ（CONSTANT を含む）の中の 32 bit の相対の参照（相対の
                                    vtable の項）。インポートには、リンカーがイメージの中のサンクを置き、そこを指す */
    NOS_X86_64_SLOT32 = 7,       /* G + A - P */
    NOS_X86_64_SLOT32_LOAD = 8,  /* G + A - P。命令は mov r64, [rip + x]。S がイメージの中なら、リンカーが lea にする */
    NOS_X86_64_TP32 = 9,         /* S + A - TP。プログラムの TLS */
    NOS_X86_64_TP_SLOT32 = 10,   /* G + A - P。スロットの中身は S - TP。ライブラリーではエラー。プログラムでは、命令が
                                    REX.W の mov r64, [rip + x] か add r64, [rip + x] なら、リンカーが mov r64, imm32 か
                                    add r64, imm32 に書き換え、ほかの形ならスロットに S - TP の値を置く */
    NOS_X86_64_TLS_DESCRIPTOR32 = 11, /* D + A - P（A は disp32 から命令の終わりまでの −4）。命令は
                                         lea rax, [rip + x] で、続く call [rax]（TLS_CALL）が rax に S - TP を返す。
                                         プログラムでは、リンカーが mov rax, S - TP（7 byte）に書き換える */
    NOS_X86_64_TLS_CALL = 12,    /* ディスクリプターの呼び出し call [rax]（2 byte）の印で、値を書かない。プログラム
                                    では、リンカーが 2 byte の nop に書き換える */
    NOS_X86_64_TLS_OFFSET32 = 13, /* S の TLS ブロック内のオフセット + A。TLS ブロックの先頭のディスクリプター
                                     （_TLS_MODULE_BASE_）と組む */
    NOS_X86_64_TLS_OFFSET64 = 14, /* 同じ。DWARF の TLS の場所 */
    NOS_X86_64_SECTION32 = 15,   /* S の出力セクションの中のオフセット + A。DWARF の部分の間の参照 */
    NOS_X86_64_SECTION64 = 16,   /* 同じ */
    NOS_X86_64_SLOT32_BRANCH = 17, /* G + A - P。命令は call [rip + x] か jmp [rip + x]。S がイメージの中なら、リンカーが
                                      直接の呼び出し（addr32 call rel32、nop と jmp rel32。どちらも 6 byte）にする */
};

/* ---- アーカイブのスライス ----
   テーブルは MEMBERS、SYMBOLS（名前の byte の昇順）、STRINGS。メンバーは完全な .NOS のオブジェクトか、ツール用の
   データで、8 に揃う。インデックスは書く側（nos ar、rustc）が作る。 */
struct nos_archive {
    uint32_t head_bytes;   /* このヘッダーの大きさ */
    uint32_t table_count;
    uint32_t table_bytes;  /* struct nos_table 一つの大きさ */
    uint32_t reserved;
    uint64_t tables;       /* struct nos_table[table_count] の、スライス先頭からのオフセット（8 に揃う） */
};
enum nos_archive_table {
    NOS_ARCHIVE_STRINGS = 1,  /* byte 列 */
    NOS_ARCHIVE_MEMBERS = 2,  /* CRITICAL。struct nos_member */
    NOS_ARCHIVE_SYMBOLS = 3,  /* CRITICAL。struct nos_archive_symbol */
};
enum nos_member_kind {
    NOS_MEMBER_OBJECT = 1,  /* .NOS のオブジェクト */
    NOS_MEMBER_DATA = 2,    /* ツール用のデータ（rustc の lib.rmeta） */
};
struct nos_member {
    uint32_t name;    /* STRINGS の中のオフセット */
    uint32_t kind;    /* enum nos_member_kind */
    uint64_t offset;  /* スライス先頭からのオフセット。8 に揃う */
    uint64_t bytes;
};
struct nos_archive_symbol {
    uint32_t name;    /* STRINGS の中のオフセット */
    uint32_t member;  /* MEMBERS の番号 */
};

/* ---- 宣言 ----
   オブジェクトの役割 DECLARATION のセクションは、名前で中身が決まり、リンカーがスライスのテーブルに移す。名前は
   下の五つのどれかで、ほかはリンカーのエラー。名前が nos. で始まるセクションの役割は、アセンブラーとコンパイラーが
   DECLARATION にする。中身はポインターを持たないレコードの並びで、レコードは 8 byte に揃い、bytes がレコードの長さ
   （詰めを含む）。

   nos.requires  struct nos_provider_record → REQUIRES。同じ名前の要求は一つにまとめる（major が違えばエラー、minor は
                 大きい方）
   nos.provides  struct nos_provider_record と旗 → PROVIDES
   nos.driver    struct nos_driver_record → DRIVER。イメージに一つ
   nos.rules     struct n_hal_term の並び → RULES。そのまま続けて並べる
   nos.kernel    struct nos_kernel_record → KERNEL。カーネルのイメージだけで、ページテーブルの段数ごとに一つ */
#define NOS_SECTION_REQUIRES "nos.requires"
#define NOS_SECTION_PROVIDES "nos.provides"
#define NOS_SECTION_DRIVER "nos.driver"
#define NOS_SECTION_RULES "nos.rules"
#define NOS_SECTION_KERNEL "nos.kernel"

/* レコードの頭。NUL 終端の UTF-8 の名前が続く。nos.provides のレコードは、名前の NUL の後ろの、レコードの先頭から
   4 に揃えた位置に uint32_t の旗（NOS_PROVIDER_*）を持つ。bytes がその位置の後ろまで届かないレコードは旗を 0 と読む。
   名前と旗の間とレコードの終わりの詰めは 0。 */
struct nos_provider_record {
    uint32_t bytes;
    uint32_t major;
    uint32_t minor;
};
struct nos_driver_record {
    uint32_t bytes;
    uint32_t flags; /* NOS_DRIVER_* */
};
/* 名前を持たないレコード。フィールドの意味は struct nos_kernel と同じ。 */
struct nos_kernel_record {
    uint32_t bytes;
    uint32_t levels;
    uint32_t flags;
    uint32_t boot_flags;
    uint64_t image_low, image_high;
    uint64_t direct_low, direct_high;
};

#if defined(__clang__)
#define NOS_CONCAT_(a, b) a##b
#define NOS_CONCAT(a, b) NOS_CONCAT_(a, b)
#define NOS_UNIQUE(prefix) NOS_CONCAT(prefix, __COUNTER__)
/* 宣言のレコードを一つ置く。名前の長さに合う型を、その場で作る。 */
#define NOS_PROVIDER_RECORD_(section_, var_, name_, major_, minor_)                                              \
    __attribute__((used, retain, section(section_))) static const struct {                                     \
        uint32_t bytes, major, minor;                                                                           \
        char name[sizeof(name_)];                                                                               \
    } __attribute__((aligned(8))) var_ = {sizeof var_, (major_), (minor_), name_}
/* このイメージは name_ のプロバイダーの (major_, minor_) を要求する。 */
#define NOS_REQUIRE(name_, major_, minor_)                                                                     \
    NOS_PROVIDER_RECORD_(NOS_SECTION_REQUIRES, NOS_UNIQUE(nos_require_), name_, major_, minor_)
/* このイメージは name_ のプロバイダーの (major_, minor_) を、旗 flags_（NOS_PROVIDER_*）で提供する。 */
#define NOS_PROVIDE_(var_, name_, major_, minor_, flags_)                                                       \
    __attribute__((used, retain, section(NOS_SECTION_PROVIDES))) static const struct {                         \
        uint32_t bytes, major, minor;                                                                           \
        char name[sizeof(name_)];                                                                               \
        uint32_t flags;                                                                                         \
    } __attribute__((aligned(8))) var_ = {sizeof var_, (major_), (minor_), name_, (flags_)}
#define NOS_PROVIDE(name_, major_, minor_, flags_) NOS_PROVIDE_(NOS_UNIQUE(nos_provide_), name_, major_, minor_, flags_)
/* このイメージはドライバー name_（フラグ NOS_DRIVER_*）。 */
#define NOS_DRIVER(name_, flags_)                                                                              \
    __attribute__((used, retain, section(NOS_SECTION_DRIVER))) static const struct {                           \
        uint32_t bytes, flags;                                                                                  \
        char name[sizeof(name_)];                                                                               \
    } __attribute__((aligned(8))) nos_driver_declared = {sizeof nos_driver_declared, (flags_), name_}
/* このイメージ（ドライバーか、デバイスを claim で所有するプログラム）がデバイスと結ぶルール：common/hal_abi.h の
   N_HAL_RULE を一つ以上。 */
#ifdef __cplusplus
#define NOS_RULES(...)                                                                                          \
    __attribute__((used, retain, section(NOS_SECTION_RULES))) static constinit const struct n_hal_term         \
        NOS_UNIQUE(nos_rules_)[] = {__VA_ARGS__}
#else
#define NOS_RULES(...)                                                                                          \
    __attribute__((used, retain, section(NOS_SECTION_RULES))) static const struct n_hal_term                   \
        NOS_UNIQUE(nos_rules_)[] = {__VA_ARGS__}
#endif
/* このカーネルは、ページテーブルが levels_ 段の時、イメージを [image_low_, image_high_) に、ダイレクトマップを
   [direct_low_, direct_high_) に置ける。boot_flags_ は、分かる struct boot_info の flags の bit 0〜15。 */
#define NOS_KERNEL(levels_, boot_flags_, image_low_, image_high_, direct_low_, direct_high_)                      \
    __attribute__((used, retain, section(NOS_SECTION_KERNEL), aligned(8))) static const struct nos_kernel_record \
        NOS_UNIQUE(nos_kernel_) = {sizeof(struct nos_kernel_record), (levels_), 0, (boot_flags_), (image_low_),     \
                                   (image_high_), (direct_low_), (direct_high_)}
#endif

#ifdef __cplusplus
#define NOS_STATIC_ASSERT static_assert
#else
#define NOS_STATIC_ASSERT _Static_assert
#endif
/* 最初の大きさ（拡張の規則）。読み込み側はこれより小さいヘッダーと要素を拒否する。構造体を大きくしても、この値は
   変えない。 */
#define NOS_MIN_FILE 56u
#define NOS_MIN_SLICE 32u
#define NOS_MIN_TABLE 32u
#define NOS_MIN_DRIVER_MANIFEST 16u
#define NOS_MIN_IMAGE 240u
#define NOS_MIN_REGION 40u
#define NOS_MIN_RELOCATION_RUN 24u
#define NOS_MIN_PROVIDER 16u
#define NOS_MIN_IMPORT 24u
#define NOS_MIN_EXPORT 16u
#define NOS_MIN_DRIVER 8u
#define NOS_MIN_RULE_TERM NOS_RULE_TERM_BYTES
#define NOS_MIN_KERNEL 40u
#define NOS_MIN_FORWARD 16u
#define NOS_MIN_REPLACEMENT 16u
#define NOS_MIN_TLS_ARGUMENT 16u
#define NOS_MIN_STACK_SIZE 16u
#define NOS_MIN_NAME 24u
#define NOS_MIN_BLOB 24u
#define NOS_MIN_OBJECT 32u
#define NOS_MIN_SECTION 72u
#define NOS_MIN_SYMBOL 32u
#define NOS_MIN_RELOCATION 24u
#define NOS_MIN_GROUP 8u
#define NOS_MIN_CONTRACT_SYMBOL 16u
#define NOS_MIN_ARCHIVE 24u
#define NOS_MIN_MEMBER 24u
#define NOS_MIN_ARCHIVE_SYMBOL 8u

/* 今の構造体の大きさ。構造体を大きくする時は、ここを新しい大きさにする（NOS_MIN_* は変えない）。 */
NOS_STATIC_ASSERT(sizeof(struct nos_file) == NOS_MIN_FILE, "struct nos_file");
NOS_STATIC_ASSERT(sizeof(struct nos_slice) == NOS_MIN_SLICE, "struct nos_slice");
NOS_STATIC_ASSERT(sizeof(struct nos_table) == NOS_MIN_TABLE, "struct nos_table");
NOS_STATIC_ASSERT(sizeof(struct nos_driver_manifest) == NOS_MIN_DRIVER_MANIFEST, "struct nos_driver_manifest");
NOS_STATIC_ASSERT(sizeof(struct nos_image) == NOS_MIN_IMAGE, "struct nos_image");
NOS_STATIC_ASSERT(sizeof(struct nos_unwind) == 8, "struct nos_unwind");
NOS_STATIC_ASSERT(sizeof(struct nos_region) == NOS_MIN_REGION, "struct nos_region");
NOS_STATIC_ASSERT(sizeof(struct nos_relocation_run) == NOS_MIN_RELOCATION_RUN, "struct nos_relocation_run");
NOS_STATIC_ASSERT(sizeof(struct nos_provider) == 24, "struct nos_provider");
NOS_STATIC_ASSERT(sizeof(struct nos_import) == 32, "struct nos_import");
NOS_STATIC_ASSERT(sizeof(struct nos_export) == 24, "struct nos_export");
NOS_STATIC_ASSERT(sizeof(struct nos_forward) == NOS_MIN_FORWARD, "struct nos_forward");
NOS_STATIC_ASSERT(sizeof(struct nos_replacement) == NOS_MIN_REPLACEMENT, "struct nos_replacement");
NOS_STATIC_ASSERT(sizeof(struct nos_tls_argument) == NOS_MIN_TLS_ARGUMENT, "struct nos_tls_argument");
NOS_STATIC_ASSERT(sizeof(struct nos_stack_size) == NOS_MIN_STACK_SIZE, "struct nos_stack_size");
NOS_STATIC_ASSERT(sizeof(struct nos_driver) == NOS_MIN_DRIVER, "struct nos_driver");
NOS_STATIC_ASSERT(sizeof(struct nos_kernel) == 48, "struct nos_kernel");
NOS_STATIC_ASSERT(sizeof(struct nos_name) == NOS_MIN_NAME, "struct nos_name");
NOS_STATIC_ASSERT(sizeof(struct nos_blob) == NOS_MIN_BLOB, "struct nos_blob");
NOS_STATIC_ASSERT(sizeof(struct nos_object) == NOS_MIN_OBJECT, "struct nos_object");
NOS_STATIC_ASSERT(sizeof(struct nos_section) == NOS_MIN_SECTION, "struct nos_section");
NOS_STATIC_ASSERT(sizeof(struct nos_symbol) == NOS_MIN_SYMBOL, "struct nos_symbol");
NOS_STATIC_ASSERT(sizeof(struct nos_relocation) == NOS_MIN_RELOCATION, "struct nos_relocation");
NOS_STATIC_ASSERT(sizeof(struct nos_group) == NOS_MIN_GROUP, "struct nos_group");
NOS_STATIC_ASSERT(sizeof(struct nos_contract_symbol) == NOS_MIN_CONTRACT_SYMBOL, "struct nos_contract_symbol");
NOS_STATIC_ASSERT(sizeof(struct nos_archive) == NOS_MIN_ARCHIVE, "struct nos_archive");
NOS_STATIC_ASSERT(sizeof(struct nos_member) == NOS_MIN_MEMBER, "struct nos_member");
NOS_STATIC_ASSERT(sizeof(struct nos_archive_symbol) == NOS_MIN_ARCHIVE_SYMBOL, "struct nos_archive_symbol");
NOS_STATIC_ASSERT(sizeof(struct nos_provider_record) == 12, "struct nos_provider_record");
NOS_STATIC_ASSERT(sizeof(struct nos_driver_record) == 8, "struct nos_driver_record");
NOS_STATIC_ASSERT(sizeof(struct nos_kernel_record) == 48, "struct nos_kernel_record");

#ifdef __cplusplus
}
#endif

#endif
