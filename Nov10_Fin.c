#include <stdio.h>    // printf 等
#include <string.h>   // strncpy 等
#include <stdbool.h>  // bool 型
#include <stdlib.h>   // abs, malloc, free

// ====== 規模上限（必要に応じて変更可） ======
#define MAX_EMP    32
#define MAX_HOURS  24
#define POS_COUNT  12   // LUNCH を含めた総ポジション数

// 列幅（ここを変えると出力の横幅を調整できます）
#define COLUMN_W 14

// ====== ポジション定義 ======
typedef enum {
    POS_NONE = -1, // 非就業（その時間は店舗不在）
    REGI1 = 0, REGI2, REGIBK, BAR, DORI,
    OS1, OS2, OS3, CS1, CS2, nu, LUNCH
} Position;

// 表示用ラベル（POS_NONE は配列外扱いなので含めない）
static const char* POS_NAME[POS_COUNT] = {
    "REGI1","REGI2","REGIBK","BAR","DORI",
    "OS1","OS2","OS3","CS1","CS2","nu","LUNCH"
};

// 安全に表示するラッパー
static const char* pos_name(Position p){
    if (p == POS_NONE) return " ";
    if ((int)p >= 0 && (int)p < POS_COUNT) return POS_NAME[p];
    return "??";
}

// ====== カテゴリ判定（連続制約に使用） ======
static inline bool is_regi(Position p){ return (p==REGI1 || p==REGI2 || p==REGIBK); }
static inline bool is_os  (Position p){ return (p==OS1   || p==OS2   || p==OS3); }
static inline bool is_cs  (Position p){ return (p==CS1   || p==CS2); }

// ====== OJTが許容されるポジション ======
// ※ 要件：OJT は CS/REGI のみ（nu はOK、LUNCHは“席としては使わない”）
static inline bool ojt_allows(Position p){
    return (p == nu) || (p == REGI1) || (p == REGI2) || is_cs(p);
}

// ====== 従業員・スケジュール構造体 ======
typedef struct {
    char name[32];
    int start; // 可勤務開始時刻（例: 15）
    int end;   // 可勤務終了時刻（例: 22） -> available for hours t where start <= t < end
} Employee;

typedef struct {
    int hours;                               // 1時間単位の時間数
    int empCount;                            // 従業員数
    Position assign[MAX_HOURS][MAX_EMP];     // 基本割当（1時間単位の表示用）
    char     half_break[MAX_HOURS][MAX_EMP]; // 'F'=前半休憩, 'L'=後半休憩, 0=通常
    bool     isOJT[MAX_EMP];                 // 各従業員がOJTかどうか
} Schedule;

// ====== 需要（時間×細目ポジションごとの必要人数） ======
static int demand[MAX_HOURS][POS_COUNT];     // demand[h][LUNCH] は「その時間に30分休憩に出す人数」

// ====== 前時間のポジションを取得 ======
static inline Position prev_of(const Schedule* sch, int hour, int empIdx){
    if (hour <= 0) return POS_NONE;
    Position p = sch->assign[hour-1][empIdx];
    return p;
}

// ====== “1時間単位”の連続制約（REGIカテゴリ連続 / OS・CS同番号連続） ======
static inline bool violates_hard_regi(const Schedule* sch, int hour, int empIdx, Position cand){
    if (hour <= 0) return false;
    Position prev = prev_of(sch, hour, empIdx);
    if (prev == POS_NONE) return false;
    return (is_regi(prev) && is_regi(cand));        // REGIカテゴリ同士の連続は禁止
}
static inline bool violates_hard_same_number(const Schedule* sch, int hour, int empIdx, Position cand){
    if (hour <= 0) return false;
    Position prev = prev_of(sch, hour, empIdx);
    if (prev == POS_NONE) return false;
    if (is_os(prev) && is_os(cand) && prev == cand) return true; // OS同番号
    if (is_cs(prev) && is_cs(cand) && prev == cand) return true; // CS同番号
    return false;
}

// ====== nu を除いた需要合計 ======
static inline int total_demand_no_nu(const int need[POS_COUNT]){
    int sum = 0;
    for (int p = 0; p < POS_COUNT; ++p) if (p != nu) sum += need[p];
    return sum;
}

// ====== 席（この時間に埋めるべき細目）リストを作成（優先度は現場に合わせて調整可） ======
static int build_joblist_for_hour(Position joblist[], int maxJobs, const int need[POS_COUNT]){
    // LUNCH は “半休憩” の表現を出力で行うため、ここでは席に含めない
    Position order[] = { REGI1, BAR, OS2, OS1, CS1, REGI2, OS3, CS2, DORI, REGIBK };
    int count = 0;
    for (int i = 0; i < (int)(sizeof(order)/sizeof(order[0])); ++i){
        Position p = order[i];
        for (int k = 0; k < need[p]; ++k){
            if (count >= maxJobs) return count;
            joblist[count++] = p;
        }
    }
    return count;
}

// ====== 曜日ダミー（ここは店データで自由に上書きしてOK） ======
typedef enum { SUNDAY=0, MONDAY, TUESDAY, WEDNESDAY, THURSDAY, FRIDAY, SATURDAY } Weekday;

// 需要セット（15時から22時まで拡張に対応）
// ここでは例として 15-22 のレンジにわたって各ポジションを有効化しています。
// 必要に応じて時間ごとの need を調整して下さい。
static void setup_demand_by_weekday(int weekday, int startHour, int hours){
    // 0クリア
    for (int h = 0; h < hours; ++h) for (int p = 0; p < POS_COUNT; ++p) demand[h][p] = 0;

    for (int h = 0; h < hours; ++h){
        int t = startHour + h;
        // 基本構成：REGI1/2/BK、BAR、DORI、OS1-3、CS1-2 を有効化
        demand[h][REGI1] = 1;
        demand[h][REGI2] = 1;
        demand[h][REGIBK]= 1;
        demand[h][BAR]   = 1;
        demand[h][DORI]  = 1;
        demand[h][OS1]   = 1;
        demand[h][OS2]   = 1;
        demand[h][OS3]   = 1;
        demand[h][CS1]   = 1;
        demand[h][CS2]   = 1;

        // 例としてピーク時間に休憩調整（15時〜22時の間で 17時/19時 に 2人休憩）
        if (t == 17 || t == 19) demand[h][LUNCH] = 2;
        else demand[h][LUNCH] = 0;
    }
}

// ====== 休憩候補選び（自動ペアリング用の候補取得） ======
// 方針：直前30分に休憩していた人（=前時間 'F' or 'L'）は避ける／nuの人を優先。
// さらに「その時間に店舗不在（POS_NONE）」の人は候補外とする。
static int pick_break_candidate(const Schedule* sch, int h, int startIdx, bool picked[], bool prefernu){
    int N = sch->empCount;
    for (int loop = 0; loop < N; ++loop){
        int i = (startIdx + loop) % N;
        if (picked[i]) continue;                       // 既にペアで選出済みは除外
        // 店舗不在は除外
        if (sch->assign[h][i] == POS_NONE) continue;
        if (h > 0 && sch->half_break[h-1][i] != 0)     // 直前時間に休憩してた人は避ける
            continue;
        if (prefernu && sch->assign[h][i] != nu)       // nu優先モード
            continue;
        return i; // 条件合致
    }
    // nu優先で見つからなければ、優先を緩めて再スキャン
    if (prefernu) return pick_break_candidate(sch, h, startIdx, picked, false);
    return -1;
}

// ====== OJT制約つき“交代/肩代わり”の安全代入ヘルパ ======
static void safe_assign_pair_with_ojt(Schedule* sch, int h, int i, int j){
    Position pos_i = sch->assign[h][i];
    Position pos_j = sch->assign[h][j];

    // 店舗不在の人が混ざっているなら何もしない（あり得ない想定だが安全策）
    if (pos_i == POS_NONE || pos_j == POS_NONE) return;

    // ケース1：i が nu で j が仕事 → j の仕事を i に肩代わり（OJTチェック）
    if (pos_i == nu && pos_j != nu){
        if (!sch->isOJT[i] || ojt_allows(pos_j)){
            sch->assign[h][i] = pos_j; // OK
        }
        // j はそのまま（後で休憩タグで前半/後半が表現される）
        return;
    }
    // ケース2：j が nu で i が仕事 → i の仕事を j に肩代わり（OJTチェック）
    if (pos_j == nu && pos_i != nu){
        if (!sch->isOJT[j] || ojt_allows(pos_i)){
            sch->assign[h][j] = pos_i; // OK
        }
        return;
    }
    // ケース3：両者とも仕事 → swap（両者が swap 後のポジションに入れるかチェック）
    if (pos_i != nu && pos_j != nu){
        bool ok_i = (!sch->isOJT[i]) || ojt_allows(pos_j);
        bool ok_j = (!sch->isOJT[j]) || ojt_allows(pos_i);
        if (ok_i && ok_j){
            Position tmp = sch->assign[h][i];
            sch->assign[h][i] = sch->assign[h][j];
            sch->assign[h][j] = tmp;
        }
        // どちらかNGなら、何もしない（元の配置のまま）
    }
}

// ====== 休憩ペアリング（demand[h][LUNCH] 人分の 30分休憩を自動で割り当てる） ======
static void make_half_break_pairs(Schedule* sch, int h){
    int want = demand[h][LUNCH];                 // この時間に半休憩へ出す人数
    if (want <= 0) return;                       // 休憩なしなら何もしない

    // 利用可能人数をカウント（POS_NONEは不在扱い）
    int available = 0;
    for (int i = 0; i < sch->empCount; ++i) if (sch->assign[h][i] != POS_NONE) available++;
    if (want > available) want = available;

    bool picked[MAX_EMP] = {false};
    int rr = 0;

    int assigned = 0;
    while (assigned < want){
        // 1人目（F側）候補を選ぶ：まずは nu 優先
        int i = pick_break_candidate(sch, h, rr, picked, true);
        if (i == -1) break;
        // ここでは i を必ず使うわけではなく、相方が見つからなければ単独 'L' に切り替える可能性がある
        // まず仮に確保
        int next_scan_start = (i + 1) % sch->empCount;

        // 2人目（L側）を選ぶ：i が OJT の場合は、後半に i が肩代わりできる相方だけ許可
        int j = -1;
        for (int loop = 0; loop < sch->empCount; ++loop){
            int cand = (next_scan_start + loop) % sch->empCount;
            if (cand == i) continue;
            if (picked[cand]) continue;
            if (sch->assign[h][cand] == POS_NONE) continue; // 不在は除外
            if (h > 0 && sch->half_break[h-1][cand] != 0) continue; // 直前休憩は避ける

            // i が OJT の場合は、相方のポジションが OJT でも可能なものに限定
            Position pos_cand = sch->assign[h][cand];
            if (sch->isOJT[i] && !ojt_allows(pos_cand)) continue;

            // 条件クリア
            j = cand;
            break;
        }

        if (j == -1){
            // 相方が見つからない：i を単独 'L'（前半勤務→後半休憩）にする
            sch->half_break[h][i] = 'L';
            picked[i] = true;
            rr = (i + 1) % sch->empCount;
            assigned += 1;
            continue;
        }

        // ペア成立：i は 'F'（前半休憩→後半出勤）、j は 'L'（前半出勤→後半休憩）
        sch->half_break[h][i] = 'F';
        sch->half_break[h][j] = 'L';
        picked[i] = picked[j] = true;
        rr = (j + 1) % sch->empCount;

        // OJT 制約を満たす形で交代/肩代わりを適用
        safe_assign_pair_with_ojt(sch, h, i, j);

        assigned += 2;
    }
}

// ====== 従業員がその時間に出勤可能か判定 ======
static inline bool emp_is_available_at(const Employee* e, int hour){
    return (hour >= e->start && hour < e->end);
}

// ====== コア：1時間単位の割当 → 休憩ペア設定 → “休憩を挟んだ連続” 解消 ======
static void build_schedule(Schedule* sch, Employee emps[], int empCount, int startHour){
    sch->empCount = empCount;
    int rr_start  = 0; // ラウンドロビン開始位置

    // 初期化：assign/half_break を安全に初期化
    for (int h = 0; h < sch->hours; ++h){
        for (int i = 0; i < empCount; ++i){
            int absHour = startHour + h;
            if (!emp_is_available_at(&emps[i], absHour)){
                sch->assign[h][i] = POS_NONE; // 店舗不在
            } else {
                sch->assign[h][i] = nu; // デフォルトで待機
            }
            sch->half_break[h][i] = 0;
        }
    }

    for (int h = 0; h < sch->hours; ++h){
        // 1) 通常のポジションを埋める（LUNCH は席としては使わない）
        bool used[MAX_EMP] = {false};
        Position joblist[MAX_EMP];
        int jobN = build_joblist_for_hour(joblist, MAX_EMP, demand[h]);

        for (int j = 0; j < jobN; ++j){
            Position target = joblist[j];
            int best = -1;
            for (int loop = 0; loop < empCount; ++loop){
                int i = (rr_start + loop) % empCount;
                if (used[i]) continue;
                // 店舗不在はスキップ
                if (sch->assign[h][i] == POS_NONE) continue;

                // ★ 追加：OJT は CS/REGI/nu 以外に入れない
                if (sch->isOJT[i] && !ojt_allows(target)) continue;

                if (violates_hard_regi(sch, h, i, target)) continue;
                if (violates_hard_same_number(sch, h, i, target)) continue;
                best = i; break;
            }
            if (best != -1){
                sch->assign[h][best] = target;
                used[best] = true;
                rr_start = (best + 1) % empCount;
            }
        }
        // 未割当は nu（既に初期化しているが念のため。POS_NONE は不在を表すので触らない）
        for (int i = 0; i < empCount; ++i){
            if (sch->assign[h][i] == POS_NONE) continue;
            if (!used[i]) sch->assign[h][i] = nu;
        }

        // 2) 休憩（30分交代）を自動でつける：demand[h][LUNCH] 人ぶん
        make_half_break_pairs(sch, h);
    }

    // 3) “休憩を挟んだ同一ポジション連続” を解消（従来ロジックを維持）
    for (int h = 0; h < sch->hours; ++h){
        for (int i = 0; i < sch->empCount; ++i){
            // 店舗不在なら何もしない
            if (sch->assign[h][i] == POS_NONE) continue;

            char tag = sch->half_break[h][i];
            // --- 前時間と今時間の関係：LUNCH->POS 側のチェック ---
            if (tag == 'F' && h > 0){
                Position cur  = sch->assign[h][i];      // 後半に入る予定のPOS
                Position prev = sch->assign[h-1][i];    // 前時間のPOS
                if ( prev == POS_NONE ){
                    // 前時間不在なら OK
                } else {
                    if ( cur == prev
                      || (is_regi(cur)&&is_regi(prev))
                      || (is_os(cur)&&is_os(prev)&&cur==prev)
                      || (is_cs(cur)&&is_cs(prev)&&cur==prev) ){
                        sch->assign[h][i] = nu;          // 休憩明けが “前時間と連続” → nuへ
                    }
                }
                // ★ OJTが禁止ポジションに流れた場合のガード
                if (sch->isOJT[i] && !ojt_allows(sch->assign[h][i])) sch->assign[h][i] = nu;
            }
            // --- 今時間と次時間の関係：POS->LUNCH 側のチェック ---
            if (tag == 'L' && h+1 < sch->hours){
                Position before = sch->assign[h][i];     // 今時間（前半）に入っているPOS
                Position next   = sch->assign[h+1][i];   // 次時間のPOS
                if ( next == POS_NONE ){
                    // 次時間不在なら OK
                } else {
                    if ( next == before
                      || (is_regi(next)&&is_regi(before))
                      || (is_os(next)&&is_os(before)&&next==before)
                      || (is_cs(next)&&is_cs(before)&&next==before) ){
                        sch->assign[h+1][i] = nu;         // 次時間が “後半と連続” → nuへ
                    }
                }
                // ★ OJTガード
                if (sch->isOJT[i] && !ojt_allows(sch->assign[h+1][i])) sch->assign[h+1][i] = nu;
            }
        }
    }
}

// ====== 出力：1時間単位、休憩は「->」表記で簡潔に見せる（横揃え対応） ======
static void print_schedule(const Schedule* sch, const Employee emps[], int startHour){
    printf("\n===== シフト割当表（細目/ハード制約版）=====\n");

    // ヘッダ（従業員名を同幅で出す）
    printf("%-*s", 6, " 時間 ");
    for (int i = 0; i < sch->empCount; ++i) {
        // ★ OJTには印（*）を付けて視認性UP（任意）
        char label[48];
        snprintf(label, sizeof(label), "%s%s", emps[i].name, sch->isOJT[i] ? "*" : "");
        printf("%-*s", COLUMN_W, label);
    }
    printf("\n");

    for (int h = 0; h < sch->hours; ++h){
        char timebuf[32];
        snprintf(timebuf, sizeof(timebuf), "%02d:00", startHour + h);
        printf("%-*s", 6, timebuf);
        for (int i = 0; i < sch->empCount; ++i){
            Position p = sch->assign[h][i];
            char tag = sch->half_break[h][i];
            char cell[64];
            if (p == POS_NONE){
                snprintf(cell, sizeof(cell), "-----");
            } else if (tag == 'F') {
                snprintf(cell, sizeof(cell), "LUNCH->%s", pos_name(p)); // 前半休憩→後半そのPOS
            } else if (tag == 'L') {
                snprintf(cell, sizeof(cell), "%s->LUNCH", pos_name(p));  // 前半そのPOS→後半休憩
            } else {
                snprintf(cell, sizeof(cell), "%s", pos_name(p));       // 通常
            }

            printf("%-*s", COLUMN_W, cell);
        }
        printf("\n");
    }
}

// ====== サンプル main（9/6(土)のメンバー例に拡張） ======
int main(void){
    // 9/6 (Sat) の例：各従業員の実働ウィンドウを与えます（start, end）
    // ここでの時刻は24時間表記、その時間帯の start <= t < end の時間帯で出勤扱いになります。
    Employee emps[] = {
        {"Taisei", 15, 22},
        {"Dai",    15, 22},
        {"Chitto", 15, 18},
        {"Yumi",   15, 21},
        {"Muro",   15, 18},
        {"Rikutaro",15,20},
        {"Natuki", 15,20},
        {"Ayana",  16,22},
        {"Kanako", 16,22},
        {"Kaisei", 17,22},
        {"Saku",   18,22},
        {"Mizuki", 18,22},
        {"Kae",    18,22}
    };
    int empCount  = sizeof(emps)/sizeof(emps[0]);

    int startHour = 15;  // 15時開始
    int hours     = 7;   // 15,16,17,18,19,20,21 の 7 時間 -> 15:00 〜 22:00 のシフト区間をカバー
    int weekday   = SATURDAY;

    // 需要をセット（demand[h][LUNCH] に「その時間に30分休憩へ出す人数」を入れる）
    setup_demand_by_weekday(weekday, startHour, hours);

    // スケジュール生成
    Schedule sch = {0};
    sch.hours = hours;

    // OJT設定：Kaisei に * を付けたので OJT 扱いにする（index 9）
    for (int i = 0; i < empCount; ++i) sch.isOJT[i] = false;
    // Kaisei を OJT にする
    for (int i = 0; i < empCount; ++i){
        if (strcmp(emps[i].name, "Kaisei") == 0) sch.isOJT[i] = true;
    }

    build_schedule(&sch, emps, empCount, startHour);

    // 出力（“LUNCH->POS / POS->LUNCH” で30分交代を可視化）
    print_schedule(&sch, emps, startHour);

    return 0;
}