#include <stdio.h>    // printf 等
#include <string.h>   // strncpy 等
#include <stdbool.h>  // bool 型

// ====== 規模上限（必要に応じて変更可） ======
#define MAX_EMP    20
#define MAX_HOURS  24
#define POS_COUNT  12   // BREAK を含めた総ポジション数

// ====== ポジション定義 ======
typedef enum {
    REGI1 = 0, REGI2, REGIBK, BAR, DORI,
    OS1, OS2, OS3, CS1, CS2, TRAIN, BREAK
} Position;

// 表示用ラベル
const char* POS_NAME[POS_COUNT] = {
    "REGI1","REGI2","REGIBK","BAR","DORI",
    "OS1","OS2","OS3","CS1","CS2","TRAIN","BREAK"
};

// ====== カテゴリ判定（連続制約に使用） ======
static inline bool is_regi(Position p){ return (p==REGI1 || p==REGI2); }
static inline bool is_os  (Position p){ return (p==OS1   || p==OS2   || p==OS3); }
static inline bool is_cs  (Position p){ return (p==CS1   || p==CS2); }

// ====== 従業員・スケジュール構造体 ======
typedef struct { char name[32]; } Employee;

typedef struct {
    int hours;                               // 1時間単位の時間数
    int empCount;                            // 従業員数
    Position assign[MAX_HOURS][MAX_EMP];     // 基本割当（1時間単位の表示用）
    char     half_BREAK[MAX_HOURS][MAX_EMP]; // 'F'=前半休憩, 'L'=後半休憩, 0=通常
} Schedule;

// ====== 需要（時間×細目ポジションごとの必要人数） ======
static int demand[MAX_HOURS][POS_COUNT];     // demand[h][BREAK] は「その時間に30分休憩に出す人数」

// ====== 前時間のポジションを取得 ======
static inline Position prev_of(const Schedule* sch, int hour, int empIdx){
    if (hour <= 0) return -1;
    return sch->assign[hour-1][empIdx];
}

// ====== “1時間単位”の連続制約（REGIカテゴリ連続 / OS・CS同番号連続） ======
static inline bool violates_hard_regi(const Schedule* sch, int hour, int empIdx, Position cand){
    if (hour <= 0) return false;
    Position prev = prev_of(sch, hour, empIdx);
    return (is_regi(prev) && is_regi(cand));        // REGIカテゴリ同士の連続は禁止
}
static inline bool violates_hard_same_number(const Schedule* sch, int hour, int empIdx, Position cand){
    if (hour <= 0) return false;
    Position prev = prev_of(sch, hour, empIdx);
    if (is_os(prev) && is_os(cand) && prev == cand) return true; // OS同番号
    if (is_cs(prev) && is_cs(cand) && prev == cand) return true; // CS同番号
    return false;
}

// ====== TRAIN を除いた需要合計 ======
static inline int total_demand_no_train(const int need[POS_COUNT]){
    int sum = 0;
    for (int p = 0; p < POS_COUNT; ++p) if (p != TRAIN) sum += need[p];
    return sum;
}

// ====== 席（この時間に埋めるべき細目）リストを作成（優先度は現場に合わせて調整可） ======
static int build_joblist_for_hour(Position joblist[], const int need[POS_COUNT]){
    // BREAK は “半休憩” の表現を出力で行うため、ここでは席に含めないのがポイント！
    Position order[] = { REGI1, REGI2, REGIBK, BAR, DORI, OS1, OS2, OS3, CS1, CS2 };
    int count = 0;
    for (int i = 0; i < (int)(sizeof(order)/sizeof(order[0])); ++i){
        Position p = order[i];
        for (int k = 0; k < need[p]; ++k) joblist[count++] = p;
    }
    return count;
}

// ====== 曜日ダミー（ここは店データで自由に上書きしてOK） ======
typedef enum { SUNDAY=0, MONDAY, TUESDAY, WEDNESDAY, THURSDAY, FRIDAY, SATURDAY } Weekday;

// 例：平日ベース。15時台だけ休憩2名（30分交代ペア1組）にして挙動確認できるように。
static void setup_demand_by_weekday(int weekday, int startHour, int hours){
    // 0クリア
    for (int h = 0; h < hours; ++h) for (int p = 0; p < POS_COUNT; ++p) demand[h][p] = 0;

    for (int h = 0; h < hours; ++h){
        int t = startHour + h;
        // 最小構成（必要に応じて増やしてOK）
        demand[h][REGI1] = 1;
        demand[h][REGI2] = 1;
        demand[h][BAR]   = 1;
        demand[h][OS1]   = 1;
        demand[h][OS2]   = 1;
        demand[h][OS3]   = 1;
        demand[h][CS1]   = 1;
        // 15時台だけ休憩を 2 人（＝前半1名/後半1名）
        demand[h][BREAK] = (t == 15) ? 2 : 0;
    }
}

// ====== 休憩候補選び（自動ペアリング用の候補取得） ======
// 方針：直前30分に休憩していた人（=前時間 'F' or 'L'）は避ける／TRAINの人を優先。
static int pick_BREAK_candidate(const Schedule* sch, int h, int startIdx, bool picked[], bool preferTrain){
    int N = sch->empCount;
    for (int loop = 0; loop < N; ++loop){
        int i = (startIdx + loop) % N;
        if (picked[i]) continue;                       // 既にペアで選出済みは除外
        if (h > 0 && sch->half_BREAK[h-1][i] != 0)     // 直前時間に休憩してた人は避ける
            continue;
        if (preferTrain && sch->assign[h][i] != TRAIN) // TRAIN優先モード
            continue;
        return i; // 条件合致
    }
    // TRAIN優先で見つからなければ、優先を緩めて再スキャン
    if (preferTrain) return pick_BREAK_candidate(sch, h, startIdx, picked, false);
    return -1;
}

// ====== 休憩ペアリング（demand[h][BREAK] 人分の 30分休憩を自動で割り当てる） ======
static void make_half_BREAK_pairs(Schedule* sch, int h){
    int want = demand[h][BREAK];                 // この時間に半休憩へ出す人数
    if (want <= 0) return;                       // 休憩なしなら何もしない

    bool picked[MAX_EMP] = {false};              // 既に休憩に選んだ人
    int rr = 0;                                  // 探索開始オフセット（簡易）

    // 1人ずつ割当（偶数インデックス→前半 'F'、奇数→後半 'L' の交互）
    for (int c = 0; c < want; ++c){
        bool preferTrain = true;                 // まず TRAIN の人から拾う
        int i = pick_BREAK_candidate(sch, h, rr, picked, preferTrain);
        if (i == -1) break;                      // 見つからない
        picked[i] = true;
        rr = (i + 1) % sch->empCount;

        // 偶数回目: 前半休憩（BREAK→POS）／奇数回目: 後半休憩（POS→BREAK）
        sch->half_BREAK[h][i] = (c % 2 == 0) ? 'F' : 'L';
    }
}

// ====== コア：1時間単位の割当 → 休憩ペア設定 → “休憩を挟んだ連続” 解消 ======
static void build_schedule(Schedule* sch, Employee emps[], int empCount){
    sch->empCount = empCount;
    int rr_start  = 0; // ラウンドロビン開始位置

    for (int h = 0; h < sch->hours; ++h){
        // まずこの時間の half_BREAK をクリア
        for (int i = 0; i < empCount; ++i) sch->half_BREAK[h][i] = 0;

        // 1) 通常のポジションを埋める（BREAKは席としては使わない）
        bool used[MAX_EMP] = {false};
        int  filled[POS_COUNT] = {0};
        Position joblist[MAX_EMP];
        int jobN = build_joblist_for_hour(joblist, demand[h]);

        for (int j = 0; j < jobN; ++j){
            Position target = joblist[j];
            int best = -1;
            for (int loop = 0; loop < empCount; ++loop){
                int i = (rr_start + loop) % empCount;
                if (used[i]) continue;
                if (violates_hard_regi(sch, h, i, target)) continue;
                if (violates_hard_same_number(sch, h, i, target)) continue;
                best = i; break;
            }
            if (best != -1){
                sch->assign[h][best] = target;
                used[best] = true;
                filled[target]++;
                rr_start = (best + 1) % empCount;
            }
        }
        // 未割当は TRAIN
        for (int i = 0; i < empCount; ++i) if (!used[i]) sch->assign[h][i] = TRAIN;

        // 2) 休憩（30分交代）を自動でつける：demand[h][BREAK] 人ぶん
        make_half_BREAK_pairs(sch, h);
    }

    // 3) “休憩を挟んだ同一ポジション連続” を解消
    //   ルール：
    //   - 前時間 POS → 今時間 (BREAK→POS) は NG（REGIカテゴリ連続・OS/CS同番号含む）
    //   - 今時間 (POS→BREAK) → 次時間 POS は NG（同上）
    for (int h = 0; h < sch->hours; ++h){
        for (int i = 0; i < sch->empCount; ++i){
            char tag = sch->half_BREAK[h][i];
            // --- 前時間と今時間の関係：BREAK→POS 側のチェック ---
            if (tag == 'F' && h > 0){
                Position cur  = sch->assign[h][i];      // 後半に入る予定のPOS
                Position prev = sch->assign[h-1][i];    // 前時間のPOS
                if ( cur == prev
                  || (is_regi(cur)&&is_regi(prev))
                  || (is_os(cur)&&is_os(prev)&&cur==prev)
                  || (is_cs(cur)&&is_cs(prev)&&cur==prev) ){
                    sch->assign[h][i] = TRAIN;          // 休憩明けが “前時間と連続” → TRAINへ
                }
            }
            // --- 今時間と次時間の関係：POS→BREAK 側のチェック ---
            if (tag == 'L' && h+1 < sch->hours){
                Position before = sch->assign[h][i];     // 今時間（前半）に入っているPOS
                Position next   = sch->assign[h+1][i];   // 次時間のPOS
                if ( next == before
                  || (is_regi(next)&&is_regi(before))
                  || (is_os(next)&&is_os(before)&&next==before)
                  || (is_cs(next)&&is_cs(before)&&next==before) ){
                    sch->assign[h+1][i] = TRAIN;         // 次時間が “後半と連続” → TRAINへ
                }
            }
        }
    }
}

// ====== 出力：1時間単位、休憩は「→」表記で簡潔に見せる ======
static void print_schedule(const Schedule* sch, const Employee emps[], int startHour){
    printf("\n===== シフト割当表（細目/ハード制約版）=====\n");
    printf("時間  ");
    for (int i = 0; i < sch->empCount; ++i) printf("%-10s", emps[i].name);
    printf("\n");

    for (int h = 0; h < sch->hours; ++h){
        printf("%02d:00 ", startHour + h);
        for (int i = 0; i < sch->empCount; ++i){
            Position p = sch->assign[h][i];
            char tag = sch->half_BREAK[h][i];
            if      (tag == 'F') printf("BREAK→%-6s", POS_NAME[p]); // 前半休憩→後半そのPOS
            else if (tag == 'L') printf("%-6s→BREAK", POS_NAME[p]);  // 前半そのPOS→後半休憩
            else                 printf("%-10s",       POS_NAME[p]); // 通常
        }
        printf("\n");
    }
}

// ====== サンプル main（店舗データに合わせて編集） ======
int main(void){
    // 従業員名は自由に編集してください
    Employee emps[MAX_EMP] = {
        {"Aiko"},{"Daichi"},{"Mika"},{"Ken"},
        {"Sara"},{"Yuta"},{"Rina"},{"Shun"}
    };
    int empCount  = 8;
    int startHour = 15;  // 表示上の開始時刻（例：15時）
    int hours     = 2;   // 例として 15時台・16時台の2時間
    int weekday   = FRIDAY;

    // 需要をセット（demand[h][BREAK] に「その時間に30分休憩へ出す人数」を入れる）
    setup_demand_by_weekday(weekday, startHour, hours);

    // スケジュール生成
    Schedule sch = {0};
    sch.hours = hours;
    build_schedule(&sch, emps, empCount);

    // 出力（“BREAK→POS / POS→BREAK” で30分交代を可視化）
    print_schedule(&sch, emps, startHour);

    return 0;
}
