# ==============================================================================
# WinRTMCPlugin – セルフコンテインド SHIORI DLL
#
# 静的リンク構成
#   ┌─────────────────────────────────────────────────────────────────────┐
#   │  winrtmc_plugin.dll                                                 │
#   │  ┌──────────────┐  ┌──────────────────────┐  ┌──────────────────┐  │
#   │  │  qjs (QuickJS│  │  quickshiori_module  │  │   winrtmc        │  │
#   │  │  静的ライブ  │  │  ("quickshiori"モジュ│  │  ("winrtmc"モジ  │  │
#   │  │  ラリ)       │  │  ール実装)           │  │  ュール実装)      │  │
#   │  └──────────────┘  └──────────────────────┘  └──────────────────┘  │
#   │             ↑ すべて静的リンク ↑                                     │
#   │  ┌──────────────────────────────────────────────────────────────┐   │
#   │  │  entry.js → [qjsc] → entry_bytecode.c (ビルド時コンパイル)  │   │
#   │  └──────────────────────────────────────────────────────────────┘   │
#   └─────────────────────────────────────────────────────────────────────┘
# ==============================================================================

# entry_bytecode.c は C コードなので C コンパイラを有効化する
enable_language(C)

# ── ① entry.js を qjsc でバイトコード C ソースにコンパイル ─────────────────────
# qjsc -m -D <module> -o <out.c> <src.js>
#   デフォルト出力は C コードファイル（-b を付けると生バイナリ）
#   -m         : ES モジュールとしてコンパイル（import/export を有効化）
#   -M <name>  : 外部 C モジュールとして宣言（コンパイル時に dummy stub を作るため
#                モジュールが解決不能になるエラーを防ぐ）
#                "quickshiori" と "winrtmc" はランタイムで C 側から登録するため必須
# 生成物: entry_bytecode.c
#   → const uint32_t qjsc_entry_size;
#   → const uint8_t  qjsc_entry[];
set(ENTRY_BYTECODE_C "${CMAKE_CURRENT_BINARY_DIR}/entry_bytecode.c")

add_custom_command(
    OUTPUT  "${ENTRY_BYTECODE_C}"
    COMMAND "$<TARGET_FILE:qjsc>"
            -m
            -M quickshiori
            -M winrtmc
            -o "${ENTRY_BYTECODE_C}"
            "${CMAKE_CURRENT_SOURCE_DIR}/entry.js"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/entry.js" qjsc
    COMMENT "Compiling entry.js to bytecode (entry_bytecode.c)"
    VERBATIM
)

# entry_bytecode.c は C コードとしてコンパイルする（C++ パーサを避ける）
set_source_files_properties("${ENTRY_BYTECODE_C}" PROPERTIES LANGUAGE C)

# ── ② DLL ターゲット定義 ──────────────────────────────────────────────────────
add_library(winrtmc_plugin SHARED
    winrtmc_plugin.cpp
    "${ENTRY_BYTECODE_C}"

    # quickshiori 本体（モジュール部分のみ再利用）
    "${CMAKE_SOURCE_DIR}/src/quickshiori_module.cpp"

    # winrtmc モジュール実装（静的組み込み）
    "${CMAKE_SOURCE_DIR}/module/winrtmc/quickjs_winrtmc.cpp"
)

# ── ③ インクルードパス ────────────────────────────────────────────────────────
target_include_directories(winrtmc_plugin PRIVATE
    "${quickjs_SOURCE_DIR}"               # quickjs.h, quickjs-libc.h
    "${CMAKE_BINARY_DIR}/src"             # 生成された version.h
    "${CMAKE_SOURCE_DIR}/src"             # quickshiori_log.h, quickshiori_module.h
    "${CMAKE_SOURCE_DIR}/module/winrtmc"  # quickjs_winrtmc.h
    "${CMAKE_CURRENT_BINARY_DIR}"         # entry_bytecode.c (参照用)
)

# ── ④ C++ 標準 & 出力設定 ──────────────────────────────────────────────────────
set_target_properties(winrtmc_plugin PROPERTIES
    CXX_STANDARD          20      # C++/WinRT は C++20 必須
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME           "winrtmc_plugin"
    PREFIX                ""
    SUFFIX                ".dll"
)

# ── ⑤ 静的リンクライブラリ ────────────────────────────────────────────────────
target_link_libraries(winrtmc_plugin PRIVATE
    qjs-libc          # std / os 組み込みモジュール実装（qjs より前に）
    qjs               # QuickJS-NG コア静的ライブラリ
    runtimeobject.lib # WinRT 基礎ランタイム（CoInitializeEx 等）
)

# ── ⑥ コンパイラオプション ────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(winrtmc_plugin PRIVATE
        /utf-8            # 統一文字セット
        /EHsc             # 標準 C++ 例外
        /W3
    )
    target_compile_definitions(winrtmc_plugin PRIVATE
        WIN32_LEAN_AND_MEAN
        WINRT_LEAN_AND_MEAN
        NOMINMAX
    )
endif()
