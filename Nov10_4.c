#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// ====== 定数設定 ======
#define MAX_EMP   20
#define MAX_HOURS 24
#define POS_COUNT 11

// ====== ポジション一覧 ======
typedef enum {
    REGI1 = 0, REGI2, REGIBK, BAR, DORI,
    OS1, OS2, OS3, CS1, CS2, TRAIN
} Position;

const char* POS_NAME[POS_COUNT] = {
    "REGI1","REGI2","REGIBK","BAR","DORI",
    "OS1","OS2","OS3","CS1","CS2","TRAIN"
};

// ====== 曜日定義 ======
typedef enum {
    SUNDAY = 0, MONDAY, TUESDAY, WEDNESDAY, THURSDAY, FRIDAY, SATURDAY
} Weekday;

// ====== 構造体 ======
typedef struct {
    char name[32];
} Employee;

typedef struct {
    int hours;
    int empCount;
    Position assign[MAX_HOURS][MAX_EMP];
} Schedule;

// ====== 需要テーブル ======
static int demand[MAX_HOURS][POS_COUNT];

// ====== カテゴリ判定 ======
bool is_regi(Position p) { return (p == REGI1 || p == REGI2); }
bool is_os(Position p)   { return (p == OS1 || p == OS2 || p == OS3); }
bool is_cs(Position p)   { return (p == CS1 || p == CS2); }

// ====== 前時間のポジション取得 ======
Position prev_of(const Schedule* sch, int hour, int empIdx) {
    if (hour <= 0) return -1;
    return sch->assign[hour - 1][empIdx];
}

// ====== 連続禁止ロジック ======
bool violates_hard_regi(const Schedule* sch, int hour, int empIdx, Position cand) {
    if (hour <= 0) return false;
    Position prev = prev_of(sch, hour, empIdx);
    return (is_regi(prev) && is_regi(cand));
}
bool violates_hard_same_number(const Schedule* sch, int hour, int empIdx, Position cand) {
    if (hour <= 0) return false;
    Position prev = prev_of(sch, hour, empIdx);
    if (is_os(prev) && is_os(cand) && prev == cand) return true;
    if (is_cs(prev) && is_cs(cand) && prev == cand) return true;
    return false;
}

// ====== 需要合計（TRAIN除外） ======
int total_demand_no_train(const int need[POS_COUNT]) {
    int sum = 0;
    for (int p = 0; p < POS_COUNT; ++p)
        if (p != TRAIN) sum += need[p];
    return sum;
}

// ====== 席リスト作成（優先順序） ======
int build_joblist_for_hour(Position joblist[], const int need[POS_COUNT]) {
    Position order[] = {REGI1, REGI2, REGIBK, BAR, DORI, OS1, OS2, OS3, CS1, CS2};
    int count = 0;
    for (int i = 0; i < 10; ++i)
        for (int k = 0; k < need[order[i]]; ++k)
            joblist[count++] = order[i];
    return count;
}

// ====== 曜日別需要設定 ======
void setup_demand_by_weekday(int day, int startHour, int hours) {
    for (int h = 0; h < hours; ++h)
        for (int p = 0; p < POS_COUNT; ++p)
            demand[h][p] = 0;

    if (day >= MONDAY && day <= FRIDAY) {
        // 平日
        for (int h = 0; h < hours; ++h) {
            int time = startHour + h;
            if (time >= 15 && time < 17) {
                demand[h][REGI1]=1; demand[h][REGI2]=1; demand[h][BAR]=1;
                demand[h][OS1]=1;  demand[h][CS1]=1;
            } else if (time >= 17 && time < 19) {
                demand[h][REGI1]=1; demand[h][REGI2]=1; demand[h][REGIBK]=1;
                demand[h][BAR]=1;  demand[h][DORI]=1;
                demand[h][OS1]=1;  demand[h][OS2]=1;
                demand[h][CS1]=1;  demand[h][CS2]=1;
            } else if (time >= 19 && time < 21) {
                demand[h][REGI1]=1; demand[h][REGI2]=1; demand[h][REGIBK]=1;
                demand[h][BAR]=1;  demand[h][DORI]=1;
                demand[h][OS1]=1;  demand[h][OS2]=1;  demand[h][OS3]=1;
                demand[h][CS1]=1;  demand[h][CS2]=1;
            } else {
                demand[h][REGI1]=1; demand[h][REGI2]=1;
                demand[h][BAR]=1; demand[h][OS1]=1; demand[h][CS1]=1;
            }
        }
    } else {
        // 土日
        for (int h = 0; h < hours; ++h) {
            int time = startHour + h;
            if (time >= 13 && time < 16) {
                demand[h][REGI1]=1; demand[h][REGI2]=1;
                demand[h][BAR]=1;  demand[h][OS1]=1; demand[h][CS1]=1;
            } else if (time >= 16 && time < 19) {
                demand[h][REGI1]=1; demand[h][REGI2]=1; demand[h][REGIBK]=1;
                demand[h][BAR]=1;  demand[h][DORI]=1;
                demand[h][OS1]=1;  demand[h][OS2]=1;
                demand[h][CS1]=1;  demand[h][CS2]=1;
            } else {
                demand[h][REGI1]=1; demand[h][REGI2]=1;
                demand[h][BAR]=1; demand[h][OS1]=1; demand[h][CS1]=1;
            }
        }
    }
}

// ====== スケジュール構築 ======
void build_schedule(Schedule* sch, Employee emps[], int empCount) {
    sch->empCount = empCount;
    int rr_start = 0;

    for (int h = 0; h < sch->hours; ++h) {
        bool used[MAX_EMP] = {false};
        int filled[POS_COUNT] = {0};
        int needSum = total_demand_no_train(demand[h]);
        if (needSum > empCount)
            printf("[WARN %02d:00] 需要 %d人 > 従業員 %d人\n", h+15, needSum, empCount);

        Position joblist[MAX_EMP];
        int jobN = build_joblist_for_hour(joblist, demand[h]);

        for (int j = 0; j < jobN; ++j) {
            Position target = joblist[j];
            int bestIdx = -1;
            for (int loop = 0; loop < empCount; ++loop) {
                int i = (rr_start + loop) % empCount;
                if (used[i]) continue;
                if (violates_hard_regi(sch, h, i, target)) continue;
                if (violates_hard_same_number(sch, h, i, target)) continue;
                bestIdx = i; break;
            }
            if (bestIdx != -1) {
                sch->assign[h][bestIdx] = target;
                used[bestIdx] = true;
                filled[target]++;
                rr_start = (bestIdx + 1) % empCount;
            }
        }

        for (int i = 0; i < empCount; ++i)
            if (!used[i]) sch->assign[h][i] = TRAIN;

        for (int p = 0; p < POS_COUNT; ++p)
            if (p != TRAIN && demand[h][p] > filled[p])
                printf("[INFO %02d:00] %s 不足 %d人\n", h+15, POS_NAME[p], demand[h][p]-filled[p]);
    }
}

// ====== 出力 ======
void print_schedule(const Schedule* sch, const Employee emps[]) {
    printf("\n===== シフト割当表 =====\n");
    printf("時間  ");
    for (int i = 0; i < sch->empCount; ++i)
        printf("%-8s", emps[i].name);
    printf("\n");

    for (int h = 0; h < sch->hours; ++h) {
        printf("%02d:00 ", h+15);
        for (int i = 0; i < sch->empCount; ++i)
            printf("%-8s", POS_NAME[sch->assign[h][i]]);
        printf("\n");
    }
}

void print_rotation_checks(const Schedule* sch, const Employee emps[]) {
    printf("\n===== 連続チェック =====\n");
    bool ok = true;
    for (int h = 1; h < sch->hours; ++h)
        for (int i = 0; i < sch->empCount; ++i) {
            Position prev = sch->assign[h-1][i], cur = sch->assign[h][i];
            if (is_regi(prev)&&is_regi(cur))
                { printf("[NG] %s REGI連続 %02d→%02d (%s→%s)\n", emps[i].name, h+14, h+15, POS_NAME[prev], POS_NAME[cur]); ok=false; }
            if ((is_os(prev)&&is_os(cur)&&prev==cur)||(is_cs(prev)&&is_cs(cur)&&prev==cur))
                { printf("[NG] %s 同番号連続 %02d→%02d (%s→%s)\n", emps[i].name, h+14, h+15, POS_NAME[prev], POS_NAME[cur]); ok=false; }
        }
    if(ok) printf("連続禁止違反なし\n");
}

// ====== メイン関数 ======
int main(void) {
    Employee emps[MAX_EMP] = {
        {"Aiko"},{"Daichi"},{"Mika"},{"Ken"},{"Sara"},{"Yuta"},{"Rina"},{"Shun"}
    };
    int empCount = 8;

    int startHour = 15;  // 表示上の開始時刻
    int hours = 7;       // 営業時間(15〜21時)
    int weekday = FRIDAY; // ←ここを MONDAY〜SUNDAY に変更して実行

    setup_demand_by_weekday(weekday, startHour, hours);

    Schedule sch = {0};
    sch.hours = hours;

    build_schedule(&sch, emps, empCount);
    print_schedule(&sch, emps);
    print_rotation_checks(&sch, emps);

    return 0;
}
