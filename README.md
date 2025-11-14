# SB_Position

私はとある飲食店でアルバイトをしており

規則として1時間ごとに担当ポジションを入れ替える必要がある

ポジション配置を自動で最適化するプログラムを作成したい

ただし以下のような課題・条件が存在する

・同じポジションを連続で担当しない

・休憩時間を考慮する(休憩時間は最大で30min)

・スキル差があり忙しい時間には強い人を作成側に配置

・忙しくない時間帯にはトレーニングモード

------------------------

----- 追記 (Nov13) -----

 実際にWebアプリ開発の経験があるuniuni君と方針を決めました

・ frontend : type script (react nextJS)

・ buckend : types script (nodeJS express) & prisms 

・ 出勤時間はfrondendでその都度与える

・ その人の能力はtableで作成 ( User Table : id, name, ability )

・　GoogleCloudのデータベースと認証機能を使う ??
