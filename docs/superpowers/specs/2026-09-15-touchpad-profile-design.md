# Touchpad プロファイル(独立プロファイルとしての復活)設計

- Status: Draft (承認待ち)
- Date: 2026-09-15
- Target version: 0.4.0(非破壊的な追加のみのため MINOR バンプ)

## 背景

`fa55417`(本セッション直前のコミット、0.3.0)で Joystick と Touchpad の
Application Collection が libaoahid から削除された。削除前の Touchpad は
独立プロファイルではなく、`aoahid_touch_options.touchpad_button_count` を
0 より大きくすることで Touchscreen プロファイル (`AOAHID_PROFILE_TOUCHSCREEN`)
に相乗りする設計だった(この統合自体は 0.1.0 の時点で既に行われていた)。

今回のユーザー要望は、Touchpad を「Touchscreen のオプション」ではなく
**独立した公開プロファイル**として再設計することであり、単なる巻き戻しでは
ない。公開 API・ABI・両言語バインディング・golden descriptor・ドキュメント
全域に波及する。

## ゴール

- `AOAHID_PROFILE_TOUCHPAD` を新設し、`aoahid_spec_create_touchpad()` /
  `aoahid_touchpad_options` / `aoahid_touchpad_button()` を公開 API として
  追加する。
- Touchscreen と Touchpad は接触点(contact)系フィールドをほぼ完全に共有
  するため、内部実装は共通化する。ただし公開 API・ABI 面では
  `aoahid_touch_options` に一切手を入れない(Touchscreen 利用者への影響ゼロ)。
- 既存の公開シグネチャは変更しない(純追加、非破壊的変更)。

## 非ゴール

- Touchscreen 側 (`aoahid_touch_options` / `aoahid_spec_create_touchscreen`)
  の変更。
- Android ハードウェア上での動作検証(このライブラリの他プロファイル同様
  `[Unverified on hardware]` のまま)。

## 公開 API

### `include/aoahid.h`

```c
typedef int32_t aoahid_profile_kind;
enum {
    AOAHID_PROFILE_KEYBOARD = 1,
    AOAHID_PROFILE_MOUSE = 2,
    AOAHID_PROFILE_TOGGLE = 3,
    AOAHID_PROFILE_GAMEPAD = 4,
    AOAHID_PROFILE_TOUCHSCREEN = 5,
    AOAHID_PROFILE_PEN = 6,
    AOAHID_PROFILE_BATTERY = 7,
    AOAHID_PROFILE_RAW = 8,
    /* Always the Digitizers / Touch Pad Application Collection Usage. */
    AOAHID_PROFILE_TOUCHPAD = 9
};
```

既存の値は一切変更しない。末尾に追記するのみ。

```c
/* Touchscreen と同じ fixed-slot Multi-Touch report shape に加え、Touch Pad
 * Application Collection の物理クリックボタンを宣言する。button_count は
 * 0 (ボタンレス・クリックパッド) を許容する。button_count > 0 の場合のみ
 * aoahid_touchpad_button が有効になる。 */
typedef struct aoahid_touchpad_options {
    uint32_t struct_size;
    uint32_t reserved;
    aoahid_report_id_option report_id;
    uint32_t maximum_contacts;
    uint32_t contacts_per_report;
    aoahid_integer_field contact_identifier;
    aoahid_integer_field x;
    aoahid_integer_field y;
    aoahid_integer_field contact_count;
    uint32_t enable_pressure;
    aoahid_integer_field pressure;
    uint32_t enable_width;
    aoahid_integer_field width;
    uint32_t enable_height;
    aoahid_integer_field height;
    uint32_t enable_azimuth;
    aoahid_integer_field azimuth;
    uint32_t enable_scan_time;
    aoahid_integer_field scan_time;
    uint32_t scan_time_unit_100us;
    uint32_t enable_contact_count_maximum_feature_declaration;
    uint32_t enable_multi_packet_frames;
    /* mouse/gamepad と同じ命名。0 はボタンレス・クリックパッドを表す。 */
    uint32_t button_count;
} aoahid_touchpad_options;
```

`aoahid_touch_options` とフィールド順序・意味を完全に一致させる
(`button_count` のみ末尾に追加)。これは「内部で共通変換する」実装上の
都合であると同時に、利用者が Touchscreen 用に組んだオプション値をほぼ
そのまま Touchpad 用に転用できるという実用上の理由でもある。

```c
AOAHID_API aoahid_result AOAHID_CALL
aoahid_spec_create_touchpad(const aoahid_touchpad_options* options, aoahid_spec** out_spec);

AOAHID_API aoahid_result AOAHID_CALL
aoahid_touchpad_button(aoahid_node* node, uint32_t button, uint32_t pressed);
```

- `aoahid_spec_create_touchpad` のドキュメンテーションコメントは既存の
  `aoahid_spec_create_touchscreen` を踏襲(Ownership/Blocking/Synchronization/
  Returns の4区分)。「Always the Digitizers / Touch Pad Application
  Collection」と明記する。
- `aoahid_touchpad_button` は 0.3.0 で削除された旧 `aoahid_touchpad_button`
  とほぼ同じドキュメンテーションコメントを復元する
  (1-based button、pressed は 0/1、in-flight/multi-packet フレーム中は
  `AOAHID_ERR_BUSY`)。ただし `usable(node, AOAHID_PROFILE_TOUCHPAD, ...)` で
  Touchpad Node 専用にガードする点が旧実装(`AOAHID_PROFILE_TOUCHSCREEN` で
  ガード)と異なる。

### `include/aoahid.hpp`(C++ラッパー)

```cpp
/* Always fixed-slot Multi-Touch under the Touch Screen Application Collection. */
class touchscreen_node_ref final : public node_ref {
  public:
    constexpr touchscreen_node_ref() noexcept = default;
    [[nodiscard]] aoahid_result touch(std::uint32_t contact_id, bool down, std::int32_t x,
                                      std::int32_t y,
                                      const aoahid_touch_extra* extra = nullptr) const noexcept {
        return aoahid_touch(value_, contact_id, down ? 1U : 0U, x, y, extra);
    }
  private:
    explicit constexpr touchscreen_node_ref(aoahid_node* value) noexcept : node_ref(value) {}
    friend aoahid_result bind(aoahid_node*, touchscreen_node_ref&) noexcept;
};

/* Always fixed-slot Multi-Touch under the Touch Pad Application Collection.
 * button() is only meaningful when the Spec declared button_count above zero. */
class touchpad_node_ref final : public node_ref {
  public:
    constexpr touchpad_node_ref() noexcept = default;
    [[nodiscard]] aoahid_result touch(std::uint32_t contact_id, bool down, std::int32_t x,
                                      std::int32_t y,
                                      const aoahid_touch_extra* extra = nullptr) const noexcept {
        return aoahid_touch(value_, contact_id, down ? 1U : 0U, x, y, extra);
    }
    [[nodiscard]] aoahid_result button(std::uint32_t index, bool pressed) const noexcept {
        return aoahid_touchpad_button(value_, index, pressed ? 1U : 0U);
    }
  private:
    explicit constexpr touchpad_node_ref(aoahid_node* value) noexcept : node_ref(value) {}
    friend aoahid_result bind(aoahid_node*, touchpad_node_ref&) noexcept;
};
```

`touchscreen_node_ref` は無変更(ボタン関数を生やさない)。`bind()` の
`aoahid_profile_kind` ディスパッチ(既存の `switch`/`if` 群、
`touchscreen_node_ref` 用の実装を参照)に `AOAHID_PROFILE_TOUCHPAD` の分岐を
追加する。

## 内部共有設計

### `src/api/internal.hpp`

```cpp
/* Touchscreen と Touchpad の両方が持つ、接触点(contact)系フィールドの共通表現。
 * button_count は常にここに持ち、Touchscreen 側は常に 0 を渡す。こうすると
 * has_non_neutral_state / neutralize_touch などの既存ボタン処理コードが
 * profile_kind分岐なしで両プロファイルに自然に効く。 */
struct TouchFields {
    aoahid_report_id_option report_id{};
    uint32_t maximum_contacts{};
    uint32_t contacts_per_report{};
    aoahid_integer_field contact_identifier{};
    aoahid_integer_field x{};
    aoahid_integer_field y{};
    aoahid_integer_field contact_count{};
    uint32_t enable_pressure{};
    aoahid_integer_field pressure{};
    uint32_t enable_width{};
    aoahid_integer_field width{};
    uint32_t enable_height{};
    aoahid_integer_field height{};
    uint32_t enable_azimuth{};
    aoahid_integer_field azimuth{};
    uint32_t enable_scan_time{};
    aoahid_integer_field scan_time{};
    uint32_t scan_time_unit_100us{};
    uint32_t enable_contact_count_maximum_feature_declaration{};
    uint32_t enable_multi_packet_frames{};
    uint32_t button_count{};
};

struct TouchConfig {
    TouchFields fields{};
    /* profile_kind が Touchscreen か Touchpad かを状態機械側で区別する必要は
     * ないが、診断メッセージ("touch." vs "touchpad.") 用に保持する。 */
    aoahid_profile_kind profile_kind{};
};
```

`TouchConfig::options`(旧: `aoahid_touch_options` そのまま保持)は
`TouchConfig::fields`(`TouchFields`)に置き換える。`aoahid_touch_options` /
`aoahid_touchpad_options` はどちらも spec.cpp 側で `TouchFields` に変換して
から共通実装へ渡すので、`state.cpp`・`c_api.cpp` の記述子生成後の状態機械
コードは `options->x` 等への参照を `fields.x` に変える以外、変更不要。

`TouchState` はボタン state を復元する(0.3.0 で削除された分):

```cpp
struct TouchState {
    std::array<ContactState, 16> contacts{};
    std::vector<std::uint8_t> buttons;
    std::vector<std::uint8_t> button_transitions;
    std::uint32_t scan_time{};
    std::size_t packet_cursor{};
    std::chrono::steady_clock::time_point scan_epoch{};
    bool scan_epoch_active{};
};
```

`button_count == 0` の場合は `buttons`/`button_transitions` が空ベクタに
なるだけで、既存の `std::any_of` ベースの判定はすべて偽を返す
(0.3.0 で削除される前のコードと同型)。profile_kind による分岐は不要。

### `src/profiles/spec.cpp`

- `create_touch_spec` を `TouchFields` を受け取る共通関数
  `create_touch_spec_from_fields(const TouchFields& fields, aoahid_profile_kind profile_kind, aoahid_spec** out_spec)`
  にリファクタリングする。既存のバリデーション(範囲チェック・
  `enable_*` と対応フィールドの整合性チェックなど)はそのまま `fields.*`
  参照に変更するだけで、ロジックの変更は発生しない。
- 記述子生成のラムダは、`begin_application` に渡す Usage を
  `profile_kind == AOAHID_PROFILE_TOUCHPAD ? aoa::hid::usage::touch_pad : aoa::hid::usage::touch_screen`
  で切り替える。ボタンフィールドの emit
  (`builder.variable_range(page_button, 1, button_count, 1, FieldSemantic::buttons)`)
  は `fields.button_count > 0` のときのみ行う(0.3.0 で削除される前と同じ
  コード)。
- `aoahid_spec_create_touchscreen_impl` は
  `aoahid_touch_options` → `TouchFields`(`button_count = 0` 固定)に変換して
  共通関数を呼ぶ薄いラッパーになる。
- 新設 `aoahid_spec_create_touchpad_impl` は
  `aoahid_touchpad_options` → `TouchFields`(`button_count = options->button_count`)
  に変換して共通関数を呼ぶ。`button_count > 65535U` は
  `AOAHID_ERR_OVERFLOW`(0.3.0 で削除された旧チェックを復元)。
- Android status: Touchscreen 側は現状どおり常に
  `AOAHID_ANDROID_PORTABLE_CANDIDATE`。Touchpad 側は
  `button_count` の有無に関わらず常に `AOAHID_ANDROID_CONDITIONAL`
  (削除コミットの根拠 — Android は Touchpad の contact を mouse-source の
  MotionEvent に変換するため、Touch Pad Application Collection 自体の
  ポータビリティは無条件では portable candidate と言えない、という判断を
  踏襲する)。

### `src/hid/usages.hpp`

`touch_pad = 0x05U`(Digitizers page)を復元する。

### `src/api/c_api.cpp` / `src/profiles/state.cpp`

- `initialize_node_state` の `AOAHID_PROFILE_TOUCHSCREEN` ケースに加え
  `AOAHID_PROFILE_TOUCHPAD` ケースを追加、どちらも
  `TouchConfig::fields.button_count` から `buttons`/`button_transitions`
  をサイズ確保する共通処理にする(実質1本化できる)。
- `has_non_neutral_state` / `neutralize_touch` /
  `consume_lifecycle_transitions` / `serialize_node` の `FieldSemantic::buttons`
  ケースは 0.3.0 で削除された実装をそのまま復元する(`profile_kind` 分岐
  不要、`touch->buttons` が空なら自然に何もしない)。
- 新設 `aoahid_touchpad_button_impl` は 0.3.0 で削除された
  `aoahid_touchpad_button_impl` とほぼ同一。`usable()` の第2引数を
  `AOAHID_PROFILE_TOUCHPAD` にする点のみ差分。

## バリデーション・ドキュメントコメント方針

- 既存 `aoahid_touch_options` のバリデーションエラーの `field` 文字列
  (`"touch.x"` 等)は変更しない。
- 新設 `aoahid_touchpad_options` のバリデーションエラーは `"touchpad.x"`
  のように接頭辞を変える(共通実装内で `profile_kind` に応じてプレフィックス
  を切り替える、または呼び出し側でエラーの field 名を書き換える)。
- ドキュメンテーションコメント(Ownership/Blocking/Synchronization/Returns)
  は `aoahid_spec_create_touchscreen` の文面をテンプレートとして流用する。

## テスト・ゴールデン・バインディングへの影響

- `tests/abi/layout_oracle.c`: `aoahid_touchpad_options` の struct レイアウト
  検証を追加。
- `tests/hidtools/export_descriptors.cpp`: `touchpad` (button_count=2 の例)
  の記述子を `tests/golden/touchpad.hex` として出力するコードを追加。
- `tests/abi/check_v0_3_0_golden.py` → `tests/abi/check_v0_4_0_golden.py`
  にリネームし、新プロファイル・新構造体を凍結スキーマに追加。
- `tests/unit/test_profiles.cpp` / `tests/integration/test_transport.cpp`:
  0.3.0 で削除された Touchpad 関連テスト(記述子検証・
  `aoahid_touchpad_button` の state machine テスト)を、新 API 名に合わせて
  復元する。
- `bindings/python/aoahid/native.py`、`bindings/csharp/AoaHid/Native.cs`、
  `bindings/rust/aoahid-sys/src/lib.rs` / `bindings/rust/aoahid/src/lib.rs`:
  新構造体・新関数・新 enum 値を追加。`tools/check_bindings.py` がこれを
  検証する。
- `examples/c/profiles/touchpad.c` を新設し、`examples/c/multi_profile.c`
  に追記。Python/C#/Rust の `multi_profile` サンプルにも同様に追記。
- `tools/generate-support-table/generate.py` に Touchpad 行を追加。

## ドキュメント更新

`docs/PROFILES.md`、`API.md`、`DESIGN.md`、`FACT_AUDIT.md`、`LIMITS.md`、
`EXAMPLES.md`、`SOURCE_CONFLICTS.md`、`TARGET_MATRIX.md`、`README.md`、
`CHANGELOG.md` を更新する。特に `PROFILES.md` の自動化監査表に Touchpad の
行を独立して追加し(Touchscreen の行とは別)、Android 分類根拠
(`AOAHID_ANDROID_CONDITIONAL` である理由)を明記する。
`docs/AOA_HID_GUIDE.md` は一般的なプラットフォーム参考資料のため変更不要。

## バージョニング

`AOAHID_VERSION_MINOR` を `3` → `4` に上げ、`CMakeLists.txt` の
`project(... VERSION 0.4.0 ...)`、`vcpkg.json`、`Doxyfile`、
Python/C#/Rust の各パッケージマニフェストを揃える。`CHANGELOG.md` に
`[0.4.0]` エントリを追加する。

## 決定事項(承認済み)

- `button_count = 0`(ボタンレス・クリックパッド)を許容する。
- バージョンは 0.3.0 → 0.4.0。
- ABI・バインディング・golden descriptor・ドキュメントを含むフル実装で
  進める。

## 実装詳細の確定

- 共通化は「`TouchFields` と `aoahid_profile_kind` を受け取る1つの内部関数
  (`create_touch_spec_from_fields`)」に一本化する。Usage の切り替え・
  エラー接頭辞(`"touch."` / `"touchpad."`)の切り替えはこの関数内で
  `profile_kind` により分岐させる。呼び出し側(`aoahid_spec_create_touchscreen_impl`
  / `aoahid_spec_create_touchpad_impl`)はオプション構造体を `TouchFields`
  に変換して渡すだけの薄いラッパーとする。
- `tests/golden/touchpad.hex` は旧 `touchpad-fixed-mt.hex` 生成時と同じ
  デフォルトオプション値(`button_count = 2`)を踏襲する。
