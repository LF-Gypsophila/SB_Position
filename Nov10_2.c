#include <stdio.h>    // 入出力に必要
#include <string.h>   // 文字列操作に必要
#include <stdbool.h>  // 真偽値(bool)に必要

// ====== 上限定義（店舗規模に合わせて調整可） ======
#define MAX_EMP   20       // 登録する従業員の最大数
#define MAX_HOURS 24       // スケジュール最大時間数（1時間単位）
#define POS_COUNT 9        // ポジション細目の総数

// ====== 細目ポジション定義（表示はこの順） ======
typedef enum {
    REGI1 = 0,    // レジ1
    REGI2 = 1,    // レジ2
    BAR   = 2,    // バー
    OS1   = 3,    // OS1
    OS2   = 4,    // OS2
    OS3   = 5,    // OS3
    CS1   = 6,    // CS1
    CS2   = 7,    // CS2
    TRAIN = 8     // トレーニング/余剰
} Position;

// ====== 表示用のポジション名（printfで表にする） ======
const char* POS_NAME[POS_COUNT] = {
    "REGI1","REGI2","BAR","OS1","OS2","OS3","CS1","CS2","TRAIN"
};

// ====== 従業員の簡易構造体（必要ならスキル等を拡張可） ======
typedef struct {
    char name[32];  // 従業員名
} Employee;

// ====== スケジュール格納（[時間][従業員] に割当細目を保持） ======
typedef struct {
    int hours;                              // 時間数
    int empCount;                           // 従業員数
    Position assign[MAX_HOURS][MAX_EMP];    // 割当結果
} Schedule;

// ====== 需要テーブル：demand[hour][position] = 必要人数 ======
static int demand[MAX_HOURS][POS_COUNT];    // サンプルは setup_example() で設定

// ====== 便宜関数：カテゴリ判定 ======
bool is_regi(Position p) {                  // REGIカテゴリか？
    return (p == REGI1 || p == REGI2);      // REGI1/REGI2 のいずれかなら true
}
bool is_os(Position p) {                    // OSカテゴリか？
    return (p == OS1 || p == OS2 || p == OS3); // OS1/2/3 のいずれかなら true
}
bool is_cs(Position p) {                    // CSカテゴリか？
    return (p == CS1 || p == CS2);          // CS1/CS2 のいずれかなら true
}

// ====== 直前のポジション取得（h=0 のときは無効） ======
Position prev_of(const Schedule* sch, int hour, int empIdx) {
    if (hour <= 0) return -1;               // 最初の時間は過去なし
    return sch->assign[hour - 1][empIdx];   // 直前の割当を返す
}

// ====== ハード制約1：REGIカテゴリの連続禁止（REGI→REGI はNG） ======
bool violates_hard_regi(const Schedule* sch, int hour, int empIdx, Position cand) {
    if (hour <= 0) return false;            // 初回は制約なし
    Position prev = prev_of(sch, hour, empIdx); // 直前のポジションを取得
    if (prev == -1) return false;           // 念のためガード
    if (is_regi(prev) && is_regi(cand)) {   // 前回も今回もREGIカテゴリなら
        return true;                        // 連続禁止に抵触
    }
    return false;                           // それ以外はOK
}

// ====== ハード制約2：OS/CS の“同番号”連続禁止（例：OS1→OS1 はNG） ======
bool violates_hard_same_number(const Schedule* sch, int hour, int empIdx, Position cand) {
    if (hour <= 0) return false;            // 初回は制約なし
    Position prev = prev_of(sch, hour, empIdx); // 直前のポジションを取得
    if (prev == -1) return false;           // 念のためガード
    // OSカテゴリ同士で“番号まで同じ”なら禁止
    if (is_os(prev) && is_os(cand) && prev == cand) return true; // OSx→同じOSx はNG
    // CSカテゴリ同士で“番号まで同じ”なら禁止
    if (is_cs(prev) && is_cs(cand) && prev == cand) return true; // CSx→同じCSx はNG
    // 番号が違う（例：OS1→OS2 / CS1→CS2）は許可
    return false;                          // 上記以外はOK
}

// ====== 需要合計（TRAINを除く） ======
int total_demand_no_train(const int need[POS_COUNT]) {
    int sum = 0;                            // 合計の初期化
    for (int p = 0; p < POS_COUNT; ++p) {   // 全ポジションを走査
        if (p == TRAIN) continue;           // TRAIN は需要対象外
        sum += need[p];                     // 必要人数を加算
    }
    return sum;                             // 合計を返す
}

// ====== joblist を作る（この時間に埋める“席”の列を作成） ======
int build_joblist_for_hour(Position joblist[], const int need[POS_COUNT]) {
    Position order[] = {                    // 優先度順（現場に合わせて調整可）
        REGI1, REGI2, BAR, OS1, OS2, OS3, CS1, CS2
    };
    int orderLen = (int)(sizeof(order)/sizeof(order[0])); // 配列長
    int count = 0;                         // joblist の要素数
    for (int oi = 0; oi < orderLen; ++oi) { // 優先度順に処理
        Position p = order[oi];            // 対象細目
        for (int k = 0; k < need[p]; ++k) {// 必要席数ぶん push
            joblist[count++] = p;          // 席を追加
        }
    }
    return count;                          // 席数を返す
}

// ====== サンプル条件（ここを店舗実態に書き換えれば即運用可） ======
void setup_example(Employee emps[], int* empCount, int* hours) {
    static const char* names[] = {         // サンプル従業員名
        "Aiko","Daichi","Mika","Ken","Sara","Yuta","Rina","Shun"
    };
    int n = (int)(sizeof(names)/sizeof(names[0])); // 名数の算出
    for (int i = 0; i < n; ++i) {          // 名前を Employee 配列にコピー
        strncpy(emps[i].name, names[i], sizeof(emps[i].name)-1); // 安全コピー
        emps[i].name[sizeof(emps[i].name)-1] = '\0';             // 終端
    }
    *empCount = n;                         // 従業員数を反映

    *hours = 2;                            // 例：2時間（15時/16時）

    // 15時台の需要（例）：REGI1=1,REGI2=1,BAR=1,OS1=1,OS2=1,OS3=0,CS1=1,CS2=1
    for (int p = 0; p < POS_COUNT; ++p) demand[0][p] = 0; // クリア
    demand[0][REGI1] = 1; demand[0][REGI2] = 1;          // レジ
    demand[0][BAR]   = 1;                                // バー
    demand[0][OS1]   = 1; demand[0][OS2] = 1;            // OS
    demand[0][OS3]   = 0;                                // OS3
    demand[0][CS1]   = 1; demand[0][CS2] = 1;            // CS
    demand[0][TRAIN] = 0;                                // TRAINは需要外

    // 16時台の需要（例）：OS3 を追加して少し増やす
    for (int p = 0; p < POS_COUNT; ++p) demand[1][p] = 0; // クリア
    demand[1][REGI1] = 1; demand[1][REGI2] = 1;          // レジ
    demand[1][BAR]   = 1;                                // バー
    demand[1][OS1]   = 1; demand[1][OS2] = 1; demand[1][OS3] = 1; // OS
    demand[1][CS1]   = 1; demand[1][CS2] = 1;            // CS
    demand[1][TRAIN] = 0;                                // TRAINは需要外
}

// ====== スケジュール構築（ハード制約を満たす貪欲＋回転） ======
void build_schedule(Schedule* sch, Employee emps[], int empCount) {
    sch->empCount = empCount;              // 従業員数を保存
    int rr_start = 0;                      // ラウンドロビン開始位置

    for (int h = 0; h < sch->hours; ++h) { // 各時間帯を処理
        bool used[MAX_EMP] = {false};      // その時間で既に割当済みか
        int  filled[POS_COUNT] = {0};      // 細目ごとの充足数
        int needSum = total_demand_no_train(demand[h]); // TRAIN除く席数合計

        if (needSum > empCount) {          // 席が人より多ければ警告
            printf("[WARN %02d:00] 需要 %d人 > 従業員 %d人。全席は埋まりません。\n",
                   h+15, needSum, empCount);
        }

        Position joblist[MAX_EMP];         // 今時間に埋める席のリスト
        int jobN = build_joblist_for_hour(joblist, demand[h]); // 席数を作成

        for (int j = 0; j < jobN; ++j) {   // 各席を1つずつ埋める
            Position target = joblist[j];  // この席の細目
            int bestIdx = -1;              // 割当候補インデックス
            int bestLoop = 1e9;            // 比較用の小さいほど良い指標（巡回順）

            // ラウンドロビン順に、ハード制約を満たす未使用者を探す
            for (int loop = 0; loop < empCount; ++loop) { // rr_start から巡回
                int i = (rr_start + loop) % empCount;     // 候補の従業員
                if (used[i]) continue;                    // 既に他席に割当済みなら除外
                if (violates_hard_regi(sch, h, i, target))      continue; // REGI連続NG
                if (violates_hard_same_number(sch, h, i, target)) continue; // 同番号連続NG

                // 条件を満たす最初期の人を選ぶ（tie-break は巡回順）
                bestIdx  = i;                              // 採用候補
                bestLoop = loop;                           // 巡回距離（小さいほうが良い）
                break;                                     // 最初に見つかった人で確定
            }

            if (bestIdx != -1) {                          // 候補が見つかったら
                sch->assign[h][bestIdx] = target;         // 割当を保存
                used[bestIdx] = true;                     // その時間で使用済みに
                filled[target]++;                         // 充足数をインクリメント
                rr_start = (bestIdx + 1) % empCount;      // 次回の起点を更新
            }
            // 候補が見つからない場合は席が埋まらない（後で不足表示）
        }

        // 未使用の従業員は TRAIN へ配置（余剰戦力）
        for (int i = 0; i < empCount; ++i) {              // 全従業員を確認
            if (!used[i]) {                               // 未割当なら
                sch->assign[h][i] = TRAIN;                // TRAIN にアサイン
                used[i] = true;                           // 使用済みに
            }
        }

        // 細目ごとの不足を表示（人数不足や制約で埋まらなかった席）
        for (int p = 0; p < POS_COUNT; ++p) {             // 全細目を確認
            if (p == TRAIN) continue;                     // TRAINは対象外
            int rem = demand[h][p] - filled[p];           // 未充足人数
            if (rem > 0) {                                // 足りなければ情報表示
                printf("[INFO %02d:00] %s が %d人 不足\n",
                       h+15, POS_NAME[p], rem);
            }
        }
    }
}

// ====== 表出力（行=時間／列=従業員） ======
void print_schedule(const Schedule* sch, const Employee emps[]) {
    printf("\n===== シフト割当表（細目/ハード制約版）=====\n"); // 見出し
    printf("時間  ");                                      // 時間ヘッダ
    for (int i = 0; i < sch->empCount; ++i) {              // 各従業員の列ヘッダ
        printf("%-8s", emps[i].name);                      // 幅8で左寄せ表示
    }
    printf("\n");                                          // 改行

    for (int h = 0; h < sch->hours; ++h) {                 // 各時間の行を出力
        printf("%02d:00 ", h+15);                          // 15時開始の例
        for (int i = 0; i < sch->empCount; ++i) {          // 各従業員のセル
            Position p = sch->assign[h][i];                // 割当細目を取得
            printf("%-8s", POS_NAME[p]);                   // 名称を表示
        }
        printf("\n");                                      // 改行
    }
}

// ====== 連続チェックの可視化（運用確認用） ======
void print_rotation_checks(const Schedule* sch, const Employee emps[]) {
    printf("\n===== 連続チェック（ハード制約判定）=====\n"); // 見出し
    bool ok = true;                                        // 問題なしフラグ
    for (int h = 1; h < sch->hours; ++h) {                 // 2時間目以降を確認
        for (int i = 0; i < sch->empCount; ++i) {          // 各従業員について
            Position prev = sch->assign[h-1][i];           // 直前
            Position cur  = sch->assign[h][i];             // 現在
            // REGIカテゴリ連続の検出
            if (is_regi(prev) && is_regi(cur)) {
                printf("[HARD NG] %s が %02d:00→%02d:00 で REGI 連続（%s→%s）\n",
                       emps[i].name, h+14, h+15, POS_NAME[prev], POS_NAME[cur]);
                ok = false;                                 // 旗を折る
            }
            // OS/CS の“同番号”連続の検出
            if ((is_os(prev) && is_os(cur) && prev == cur) ||
                (is_cs(prev) && is_cs(cur) && prev == cur)) {
                printf("[HARD NG] %s が %02d:00→%02d:00 で 同番号連続（%s→%s）\n",
                       emps[i].name, h+14, h+15, POS_NAME[prev], POS_NAME[cur]);
                ok = false;                                 // 旗を折る
            }
        }
    }
    if (ok) {                                              // 違反が無い場合
        printf("ハード制約違反は検出されませんでした。\n"); // 合格表示
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

    build_schedule(&sch, emps, empCount);  // スケジュールを構築
    print_schedule(&sch, emps);            // 表形式で出力
    print_rotation_checks(&sch, emps);     // 連続禁止の検査を出力

    // 使い方メモ
    printf("\n===== 使い方メモ =====\n");                                      // 見出し
    printf("- 需要の編集: demand[h][REGI1/REGI2/BAR/OS1/OS2/OS3/CS1/CS2] を設定\n"); // 説明
    printf("- REGI1/2 はカテゴリ連続禁止（REGI→REGI は不可）\n");                    // 説明
    printf("- OS/CS は“同番号”連続禁止（OS1→OS1/CS2→CS2 は不可、番号違いは可）\n");   // 説明
    printf("- 始時刻を変える場合は printf の +15 を希望の開始時刻に変更\n");           // 説明
    printf("- 名前や時間数は setup_example() を編集\n");                                // 説明

    return 0;                              // 正常終了
}
