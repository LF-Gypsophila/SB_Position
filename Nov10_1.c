#include <stdio.h>   // 入出力に必要
#include <string.h>  // 文字列操作に必要
#include <stdbool.h> // 真偽値型に必要

// ====== 基本設定（ここを書き換えると店舗に合わせて調整できます） ======
#define MAX_EMP 16          // 登録できる従業員の最大人数（必要に応じて増やす）
#define MAX_HOURS 24        // スケジュールを組む最大時間数（1日最大24時間を想定）
#define POS_COUNT 5         // ポジション種類数（REGI, BAR, OS, CS, TRAINING の5つ）

// ポジションを列挙体で定義（数字より読みやすい）
typedef enum {
    REGI = 0,   // レジ
    BAR  = 1,   // バー（ドリンク作り）
    OS   = 2,   // オーダーサポート/ハンドオフ（OS/OS2は人数で表現）
    CS   = 3,   // カスタマーサービス（客席/ラウンド）
    TRAINING = 4// トレーニング（余剰人員の待機/学習/支援）
} Position;

// ポジション名の文字列表現（表示用）
const char* POS_NAME[POS_COUNT] = {
    "REGI", "BAR", "OS", "CS", "TRAIN"
};

// 従業員情報（今回は名前のみで簡易化）
typedef struct {
    char name[32];      // 従業員名
} Employee;

// スケジュール表（[時間][従業員] = ポジション）を格納
typedef struct {
    int hours;                          // スケジュール時間数
    int empCount;                       // 従業員数
    Position assign[MAX_HOURS][MAX_EMP];// 割当結果を保存
} Schedule;

// ====== ここから設定例（サンプル。現場に合わせて書き換えてOK） ======
void setup_example(Employee emps[], int* empCount,
                   int* hours,
                   int demand[][POS_COUNT]) {
    // 従業員の登録（必要に応じて増減・名前変更）
    static const char* names[] = {
        "Aiko", "Daichi", "Mika", "Ken", "Sara", "Yuta", "Rina", "Shun"
    };                                       // サンプル従業員8名
    int n = (int)(sizeof(names)/sizeof(names[0])); // 名数の計算
    for (int i = 0; i < n; ++i) {                 // 各名を配列へコピー
        strncpy(emps[i].name, names[i], sizeof(emps[i].name)-1); // 名前をコピー
        emps[i].name[sizeof(emps[i].name)-1] = '\0';             // 末尾を終端
    }
    *empCount = n;                                 // 従業員数を設定

    // スケジュールの時間数（例：15時台と16時台の2時間）
    *hours = 2;                                    // 時間数（必要なら増やす）

    // 各時間帯の必要人数を設定（REGI, BAR, OS, CS, TRAINは需要に含めない）
    // 例1: 15時台（混雑少なめ：合計6）REGI2, BAR1, OS2, CS1
    demand[0][REGI] = 2; demand[0][BAR] = 1; demand[0][OS] = 2; demand[0][CS] = 1; demand[0][TRAINING] = 0; // 15時
    // 例2: 16時台（混雑多め：合計8）REGI3, BAR1, OS2, CS2
    demand[1][REGI] = 3; demand[1][BAR] = 1; demand[1][OS] = 2; demand[1][CS] = 2; demand[1][TRAINING] = 0; // 16時
}

// 需要合計を数えるヘルパー（TRAININGは需要に含めない）
int total_demand_without_training(int need[POS_COUNT]) {
    int sum = 0;                                          // 合計の初期化
    for (int p = 0; p < POS_COUNT; ++p) {                 // 各ポジションを走査
        if (p == TRAINING) continue;                      // TRAININGはスキップ
        sum += need[p];                                   // 必要人数を加算
    }
    return sum;                                           // 合計を返す
}

// 指定ポジションの未充足数を取得
int remaining_need(int filled[POS_COUNT], int need[POS_COUNT], Position p) {
    int rem = need[p] - filled[p];                        // 必要数-埋まった数
    return (rem > 0) ? rem : 0;                           // 足りない分（0未満は0）
}

// 同一時間帯に未割当の従業員を探す（未使用インデックスを返す/なければ-1）
int find_unassigned(bool used[], int empCount, int startIdx) {
    for (int k = 0; k < empCount; ++k) {                  // empCount回チェック
        int i = (startIdx + k) % empCount;                // ローテーション用オフセット
        if (!used[i]) return i;                           // 未使用ならその人を返す
    }
    return -1;                                            // 全員使用済みなら-1
}

// ある従業員が前の時間に同じポジションだったかチェック（連続禁止用）
bool is_same_as_previous(Schedule* sch, int hour, int empIdx, Position p) {
    if (hour == 0) return false;                          // 最初の時間は制約なし
    return sch->assign[hour-1][empIdx] == p;              // 直前と同じならtrue
}

// 割当アルゴリズム本体（単純で読みやすい貪欲ローテーション）
void build_schedule(Schedule* sch,
                    Employee emps[], int empCount,
                    int demand[][POS_COUNT]) {
    sch->empCount = empCount;                              // 従業員数を保存

    int rr_start = 0;                                      // ローテーション開始位置

    for (int h = 0; h < sch->hours; ++h) {                 // 各時間をループ
        bool used[MAX_EMP] = {false};                      // この時間ですでに割当済みか
        int filled[POS_COUNT] = {0};                       // 各ポジションに割り当てた人数

        // 需要合計と従業員数の比較でメッセージ（必要なら注意喚起）
        int needSum = total_demand_without_training(demand[h]); // この時間の需要合計
        if (needSum > empCount) {                          // 需要＞従業員なら…
            printf("[WARN %02d:00] 需要 %d人 > 従業員 %d人。全てを充足できません。\n",
                   h+15, needSum, empCount);              // 例では15時開始で表示
        }

        // まずは需要のあるポジションを優先的に埋める（REGI→BAR→OS→CSの順）
        // ※順序は現場の優先度に合わせて並べ替えてOK
        Position order[] = {REGI, BAR, OS, CS};           // 優先度順
        int orderLen = (int)(sizeof(order)/sizeof(order[0])); // 長さ計算

        for (int oi = 0; oi < orderLen; ++oi) {            // 優先度順に処理
            Position p = order[oi];                        // 対象ポジション
            while (remaining_need(filled, demand[h], p) > 0) { // まだ足りなければ
                int idx = find_unassigned(used, empCount, rr_start); // 未使用の従業員を探す
                if (idx == -1) break;                      // 全員割当済みなら抜ける
                if (is_same_as_previous(sch, h, idx, p)) { // 連続同一ポジションなら
                    // 連続禁止に引っかかったら次の従業員を試す
                    used[idx] = false;                     // 念のため（今回は未使用のまま）
                    rr_start = (rr_start + 1) % empCount;  // ローテーションを進める
                    continue;                              // 次の候補へ
                }
                sch->assign[h][idx] = p;                   // 割当を保存
                used[idx] = true;                          // 使用済みにマーク
                filled[p]++;                                // ポジションの埋まり数を更新
                rr_start = (idx + 1) % empCount;           // 次回開始位置を更新
            }
        }

        // まだ未割当の従業員は TRAINING へ（余剰人員の活用）
        for (int i = 0; i < empCount; ++i) {               // 全従業員をチェック
            if (!used[i]) {                                // 未割当であれば
                // 直前がTRAININGなら、別ポジションに回したいが
                // 本簡易版では TRAINING 連続を許容（必要なら拡張）
                sch->assign[h][i] = TRAINING;              // トレーニングに割当
                used[i] = true;                            // 使用済みに
            }
        }

        // もし従業員が足りず需要未充足なら、どのポジションが足りないか表示
        for (int p = 0; p < POS_COUNT; ++p) {              // 各ポジションを確認
            if (p == TRAINING) continue;                   // TRAININGは対象外
            int rem = demand[h][p] - filled[p];            // 未充足数
            if (rem > 0) {                                 // 足りなければ
                printf("[INFO %02d:00] %s が %d人 不足\n",
                       h+15, POS_NAME[p], rem);            // 不足情報を表示
            }
        }
    }
}

// スケジュールを表形式で出力
void print_schedule(Schedule* sch, Employee emps[]) {
    // ヘッダ行（時間列+各従業員名）
    printf("\n===== シフト割当表 =====\n");               // 見出し
    printf("時間  ");                                      // 時間の列ヘッダ
    for (int i = 0; i < sch->empCount; ++i) {              // 各従業員を横に並べる
        printf("%-8s", emps[i].name);                      // 左寄せ8幅で表示
    }
    printf("\n");                                          // 改行

    // 各時間の行
    for (int h = 0; h < sch->hours; ++h) {                 // 時間ごとに表示
        printf("%02d:00 ", h+15);                          // 例：15:00, 16:00 と表示
        for (int i = 0; i < sch->empCount; ++i) {          // 従業員ごとの割当を表示
            Position p = sch->assign[h][i];                // 割当の取得
            printf("%-8s", POS_NAME[p]);                   // ポジション名を表示
        }
        printf("\n");                                      // 改行
    }
}

// 直前時間とのローテーション妥当性を軽くチェック（学習用の目視補助）
void print_rotation_warnings(Schedule* sch, Employee emps[]) {
    printf("\n===== 連続同一ポジションの検査 =====\n");   // 見出し
    bool ok = true;                                        // 問題が無いかのフラグ
    for (int h = 1; h < sch->hours; ++h) {                 // 1時間目以降を確認
        for (int i = 0; i < sch->empCount; ++i) {          // 各従業員を確認
            if (sch->assign[h][i] == sch->assign[h-1][i]) {// 連続同一なら
                printf("[WARN] %s が %02d:00 と %02d:00 で %s 連続\n",
                       emps[i].name, h+14, h+15,
                       POS_NAME[sch->assign[h][i]]);       // 警告を出す
                ok = false;                                 // 問題あり
            }
        }
    }
    if (ok) {                                              // 問題が無ければ
        printf("連続同一ポジションは見つかりませんでした。\n"); // 合格のメッセージ
    }
}

// ====== エントリポイント（main関数） ======
int main(void) {
    Employee emps[MAX_EMP];                                 // 従業員配列
    int empCount = 0;                                       // 従業員数
    int hours = 0;                                          // 時間数
    int demand[MAX_HOURS][POS_COUNT] = {{0}};               // 各時間の需要

    setup_example(emps, &empCount, &hours, demand);         // 例の設定を反映

    Schedule sch = {0};                                     // スケジュール構造体を初期化
    sch.hours = hours;                                      // 時間数をセット

    build_schedule(&sch, emps, empCount, demand);           // スケジュールを構築
    print_schedule(&sch, emps);                             // 表で出力
    print_rotation_warnings(&sch, emps);                    // 連続チェック出力

    // 使い方メモ（実運用時に便利）
    printf("\n===== 使い方メモ =====\n");                  // 見出し
    printf("- 従業員の増減: setup_example() の names[] を編集\n"); // 説明
    printf("- 時間数の変更: setup_example() の *hours を編集\n");  // 説明
    printf("- 必要人数の変更: demand[h][REGI/BAR/OS/CS] を編集\n");// 説明
    printf("- OS2 を表現する場合: OS の必要人数を 2 以上に設定\n"); // 説明
    printf("- 15時開始で表示中。開始時刻を変える場合は print の +15 を調整\n"); // 説明

    return 0;                                               // 正常終了
}
