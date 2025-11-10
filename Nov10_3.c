#include <stdio.h>     // 入出力（printf等）に必要
#include <string.h>    // 文字列操作（strncpy等）に必要
#include <stdbool.h>   // 真偽値(bool)に必要

// ====== 上限（必要に応じて調整） ======
#define MAX_EMP   20        // 従業員の最大登録数
#define MAX_HOURS 24        // 1時間単位の最大時間数
#define POS_COUNT 11        // 細目ポジション総数（新規: REGIBK, DORI を追加）

// ====== 細目ポジション定義（表示はこの順） ======
typedef enum {
    REGI1 = 0,      // レジ1
    REGI2 = 1,      // レジ2
    REGIBK = 2,     // レジバック（REGI支援）※デフォは独立カテゴリ
    BAR   = 3,      // バー
    DORI  = 4,      // ドリ出し（ハンドオフ）
    OS1   = 5,      // OS1
    OS2   = 6,      // OS2
    OS3   = 7,      // OS3
    CS1   = 8,      // CS1
    CS2   = 9,      // CS2
    TRAIN = 10      // トレーニング/余剰
} Position;

// ====== 表示用の短い英字ラベル（列揃え重視。日本語説明はコメントで補足） ======
const char* POS_NAME[POS_COUNT] = {
    "REGI1",  // レジ1
    "REGI2",  // レジ2
    "REGIBK", // レジバック
    "BAR",    // バー
    "DORI",   // ドリ出し
    "OS1",    // OS1
    "OS2",    // OS2
    "OS3",    // OS3
    "CS1",    // CS1
    "CS2",    // CS2
    "TRAIN"   // トレーニング
};

// ====== 従業員の基本情報（必要に応じてスキル等を拡張可） ======
typedef struct {
    char name[32];   // 従業員名
} Employee;

// ====== スケジュール保持構造（[時間][従業員] に細目ポジションを格納） ======
typedef struct {
    int hours;                              // 時間数
    int empCount;                           // 従業員数
    Position assign[MAX_HOURS][MAX_EMP];    // 割当結果
} Schedule;

// ====== 需要テーブル（demand[hour][position] = 必要人数） ======
static int demand[MAX_HOURS][POS_COUNT];    // サンプルは setup_example() で埋める

// ====== カテゴリ判定（連続禁止ロジック用） ======
bool is_regi(Position p) {                  // REGIカテゴリか？
    // デフォルト：REGI1/REGI2のみをREGIカテゴリとする
    // もし「レジバックもREGI扱い（REGI連続禁止対象）」にしたい場合は下行を:
    // return (p == REGI1 || p == REGI2 || p == REGIBK);
    return (p == REGI1 || p == REGI2);      // 現状はレジ1/2のみ
}
bool is_os(Position p) {                    // OSカテゴリか？
    return (p == OS1 || p == OS2 || p == OS3); // OS1/2/3
}
bool is_cs(Position p) {                    // CSカテゴリか？
    return (p == CS1 || p == CS2);          // CS1/2
}

// ====== 直前のポジション取得（h=0 のときは過去なし） ======
Position prev_of(const Schedule* sch, int hour, int empIdx) {
    if (hour <= 0) return -1;               // 初回は過去が無いので無効値を返す
    return sch->assign[hour - 1][empIdx];   // 直前の割当を返す
}

// ====== ハード制約1：REGIカテゴリの連続禁止（REGI→REGI はNG） ======
bool violates_hard_regi(const Schedule* sch, int hour, int empIdx, Position cand) {
    if (hour <= 0) return false;            // 初回は制約なし
    Position prev = prev_of(sch, hour, empIdx); // 直前のポジション
    if (prev == -1) return false;           // 念のためガード
    if (is_regi(prev) && is_regi(cand)) {   // 前回も今回もREGIカテゴリなら
        return true;                        // 連続禁止に抵触
    }
    return false;                           // それ以外はOK
}

// ====== ハード制約2：OS/CS の“同番号”連続禁止（例：OS1→OS1 はNG） ======
bool violates_hard_same_number(const Schedule* sch, int hour, int empIdx, Position cand) {
    if (hour <= 0) return false;            // 初回は制約なし
    Position prev = prev_of(sch, hour, empIdx); // 直前のポジション
    if (prev == -1) return false;           // 念のためガード
    if (is_os(prev) && is_os(cand) && prev == cand) return true; // OSx→同じOSx NG
    if (is_cs(prev) && is_cs(cand) && prev == cand) return true; // CSx→同じCSx NG
    return false;                           // 番号違い/カテゴリ違いはOK
}

// ====== 需要合計（TRAINを除く）を算出 ======
int total_demand_no_train(const int need[POS_COUNT]) {
    int sum = 0;                            // 合計の初期化
    for (int p = 0; p < POS_COUNT; ++p) {   // 全ポジションを走査
        if (p == TRAIN) continue;           // TRAINは需要対象外
        sum += need[p];                     // 必要人数を加算
    }
    return sum;                             // 合計を返す
}

// ====== 今時間に埋める“席”のリスト（joblist）を作成 ======
int build_joblist_for_hour(Position joblist[], const int need[POS_COUNT]) {
    // 優先度順の例：REGI → REGIBK → BAR → DORI → OS → CS
    // ここを並べ替えれば、現場方針（どこから埋めるか）に即座に対応可能
    Position order[] = {
        REGI1, REGI2, REGIBK, BAR, DORI, OS1, OS2, OS3, CS1, CS2
    };                                      // 優先度順
    int orderLen = (int)(sizeof(order)/sizeof(order[0])); // 配列長
    int count = 0;                          // joblistの要素数
    for (int oi = 0; oi < orderLen; ++oi) { // 優先度順に処理
        Position p = order[oi];             // 対象細目
        for (int k = 0; k < need[p]; ++k) { // 必要席数ぶん繰り返し
            joblist[count++] = p;           // 席（細目）を追加
        }
    }
    return count;                           // 追加した席数を返す
}

// ====== サンプル条件（ここを店舗実態に合わせて書き換えて運用） ======
void setup_example(Employee emps[], int* empCount, int* hours) {
    // --- 従業員（サンプル8名） ---
    static const char* names[] = {          // 任意に編集可
        "Aiko","Daichi","Mika","Ken","Sara","Yuta","Rina","Shun"
    };                                      // 名前一覧
    int n = (int)(sizeof(names)/sizeof(names[0])); // 名数
    for (int i = 0; i < n; ++i) {           // Employee配列へコピー
        strncpy(emps[i].name, names[i], sizeof(emps[i].name)-1); // 安全コピー
        emps[i].name[sizeof(emps[i].name)-1] = '\0';             // 終端
    }
    *empCount = n;                          // 従業員数を反映

    // --- 時間数（例：15時台/16時台の2時間） ---
    *hours = 2;                             // 必要に応じて増やす

    // --- 需要（細目）サンプル：15時台 ---
    for (int p = 0; p < POS_COUNT; ++p) demand[0][p] = 0; // 先にクリア
    demand[0][REGI1] = 1;  // レジ1
    demand[0][REGI2] = 1;  // レジ2
    demand[0][REGIBK] = 1; // レジバック（新）
    demand[0][BAR]   = 1;  // バー
    demand[0][DORI]  = 1;  // ドリ出し（新）
    demand[0][OS1]   = 1;  // OS1
    demand[0][OS2]   = 0;  // OS2
    demand[0][OS3]   = 0;  // OS3
    demand[0][CS1]   = 1;  // CS1
    demand[0][CS2]   = 0;  // CS2
    demand[0][TRAIN] = 0;  // TRAIN（需要としては0を想定）

    // --- 需要（細目）サンプル：16時台 ---
    for (int p = 0; p < POS_COUNT; ++p) demand[1][p] = 0; // クリア
    demand[1][REGI1] = 1;  // レジ1
    demand[1][REGI2] = 1;  // レジ2
    demand[1][REGIBK] = 1; // レジバック（新）
    demand[1][BAR]   = 1;  // バー
    demand[1][DORI]  = 1;  // ドリ出し（新）
    demand[1][OS1]   = 1;  // OS1
    demand[1][OS2]   = 1;  // OS2
    demand[1][OS3]   = 0;  // OS3
    demand[1][CS1]   = 1;  // CS1
    demand[1][CS2]   = 1;  // CS2
    demand[1][TRAIN] = 0;  // TRAIN
}

// ====== スケジュール構築（ハード制約を満たす貪欲＋ラウンドロビン） ======
void build_schedule(Schedule* sch, Employee emps[], int empCount) {
    sch->empCount = empCount;               // 従業員数を保持
    int rr_start = 0;                       // ラウンドロビン開始位置

    for (int h = 0; h < sch->hours; ++h) {  // 各時間帯を処理
        bool used[MAX_EMP] = {false};       // その時間で既に割当済みか
        int  filled[POS_COUNT] = {0};       // 細目ごとの充足数
        int needSum = total_demand_no_train(demand[h]); // TRAIN除く席数合計

        if (needSum > empCount) {           // 席が人より多い場合は警告
            printf("[WARN %02d:00] 需要 %d人 > 従業員 %d人。全席は埋まりません。\n",
                   h+15, needSum, empCount); // 表示は15時開始の例
        }

        Position joblist[MAX_EMP];          // 今時間に埋めるべき“席”の配列
        int jobN = build_joblist_for_hour(joblist, demand[h]); // 席数を作成

        for (int j = 0; j < jobN; ++j) {    // 席を1つずつ埋める
            Position target = joblist[j];   // 今回埋める細目
            int bestIdx = -1;               // 採用する従業員インデックス

            // rr_start から巡回し、ハード制約を満たす未使用者を見つける
            for (int loop = 0; loop < empCount; ++loop) { // 候補を巡回
                int i = (rr_start + loop) % empCount;     // 候補の従業員
                if (used[i]) continue;                    // 既に他席で使用済みなら除外
                if (violates_hard_regi(sch, h, i, target))      continue; // REGI連続NG
                if (violates_hard_same_number(sch, h, i, target)) continue; // 同番号連続NG

                bestIdx = i;                               // 条件を満たす最初の人を採用
                break;                                     // 早期確定でOK（シンプル）
            }

            if (bestIdx != -1) {                           // 採用者が見つかったら
                sch->assign[h][bestIdx] = target;          // 割当を保存
                used[bestIdx] = true;                      // 今時間で使用済みに
                filled[target]++;                          // 充足数を加算
                rr_start = (bestIdx + 1) % empCount;       // 起点を更新（回転）
            }
            // 見つからない場合：席が埋まらない（後で不足表示）
        }

        // 未使用の従業員は TRAIN へ（余剰戦力の学習/支援）
        for (int i = 0; i < empCount; ++i) {               // 全従業員を確認
            if (!used[i]) {                                // 未割当なら
                sch->assign[h][i] = TRAIN;                 // TRAINに配置
                used[i] = true;                            // 使用済みに
            }
        }

        // 細目ごとの不足を表示（人数不足や制約で埋まらなかった席）
        for (int p = 0; p < POS_COUNT; ++p) {              // 全細目を確認
            if (p == TRAIN) continue;                      // TRAINは対象外
            int rem = demand[h][p] - filled[p];            // 未充足人数を算出
            if (rem > 0) {                                 // 足りなければ情報表示
                printf("[INFO %02d:00] %s が %d人 不足\n",
                       h+15, POS_NAME[p], rem);            // 不足内容を出力
            }
        }
    }
}

// ====== 表出力（行=時間／列=従業員） ======
void print_schedule(const Schedule* sch, const Employee emps[]) {
    printf("\n===== シフト割当表（細目+レジバック/ドリ出し対応）=====\n"); // 見出し
    printf("時間  ");                                      // 時間ヘッダ
    for (int i = 0; i < sch->empCount; ++i) {              // 各従業員の列ヘッダ
        printf("%-8s", emps[i].name);                      // 幅8で左寄せ
    }
    printf("\n");                                          // 改行

    for (int h = 0; h < sch->hours; ++h) {                 // 各時間の行
        printf("%02d:00 ", h+15);                          // 例：15時開始
        for (int i = 0; i < sch->empCount; ++i) {          // 従業員セルを出力
            Position p = sch->assign[h][i];                // 割当取得
            printf("%-8s", POS_NAME[p]);                   // ラベルを表示
        }
        printf("\n");                                      // 改行
    }
}

// ====== 連続チェックの可視化（運用確認用） ======
void print_rotation_checks(const Schedule* sch, const Employee emps[]) {
    printf("\n===== 連続チェック（ハード制約）=====\n");   // 見出し
    bool ok = true;                                        // 問題なしフラグ
    for (int h = 1; h < sch->hours; ++h) {                 // 2時間目以降を確認
        for (int i = 0; i < sch->empCount; ++i) {          // 各従業員
            Position prev = sch->assign[h-1][i];           // 直前
            Position cur  = sch->assign[h][i];             // 今回
            if (is_regi(prev) && is_regi(cur)) {           // REGI→REGI ならNG
                printf("[HARD NG] %s が %02d:00→%02d:00 で REGI 連続（%s→%s）\n",
                       emps[i].name, h+14, h+15, POS_NAME[prev], POS_NAME[cur]);
                ok = false;                                // フラグを折る
            }
            if ((is_os(prev) && is_os(cur) && prev == cur) || // OS同番号連続
                (is_cs(prev) && is_cs(cur) && prev == cur)) { // CS同番号連続
                printf("[HARD NG] %s が %02d:00→%02d:00 で 同番号連続（%s→%s）\n",
                       emps[i].name, h+14, h+15, POS_NAME[prev], POS_NAME[cur]);
                ok = false;                                // フラグを折る
            }
        }
    }
    if (ok) {                                              // 違反が無ければ
        printf("ハード制約違反は検出されませんでした。\n"); // 合格メッセージ
    }
}

// ====== main（エントリポイント） ======
int main(void) {
    Employee emps[MAX_EMP];                // 従業員配列
    int empCount = 0;                      // 従業員数
    int hours = 0;                         // 時間数

    setup_example(emps, &empCount, &hours);// サンプル条件をセット

    Schedule sch = (Schedule){0};          // 構造体を0で初期化
    sch.hours = hours;                     // 時間数を反映

    build_schedule(&sch, emps, empCount);  // スケジュール構築
    print_schedule(&sch, emps);            // 表示
    print_rotation_checks(&sch, emps);     // 連続禁止チェック

    // 使い方メモ（運用時に便利）
    printf("\n===== 使い方メモ =====\n");                                      // 見出し
    printf("- 需要の編集: demand[h][REGI1/REGI2/REGIBK/BAR/DORI/OS1/OS2/OS3/CS1/CS2] を設定\n"); // 説明
    printf("- REGI連続禁止: REGI1/REGI2 同士の連続はNG（REGIBKを含めたい場合は is_regi() を変更）\n"); // 説明
    printf("- OS/CS同番号連続禁止: OS1→OS1 / CS2→CS2 などはNG（番号違いはOK）\n");                 // 説明
    printf("- 始時刻変更: 出力中の +15 を希望の開始時刻に変更（例: 9時始まりなら h+9）\n");         // 説明
    printf("- 優先順位変更: build_joblist_for_hour() の order[] を並べ替え\n");                       // 説明

    return 0;                              // 正常終了
}
