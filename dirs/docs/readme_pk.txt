MAME Plus!ベース

コア関係及びその他の修正
 フルスクリーンチャット、Kaillera対応 [EmeraldMame]
 お気に入りでネットプレイ、チャットでIME使用のGUI追加
 kailleraプレイ時の麻雀入力に対応
 バージョンチェック、ディップ送信、位置移動、ステートセーブ、ステートロード実装
 kailleraプレイ時のリプレイ保存に対応（ゲーム開始時に左Shift+左Ctrlでトグル動作）
 同期チェックとオートセーブ時の自動同期チェック実装
 Kailleraオプションダイアログ追加
 チャット文字入力時は文字入力以外のキーボードの入力を無効に
 リプレイ保存及び再生にtrc形式追加（リセット、ステートセーブ・ロードに対応）
 ローカルプレイを自動でリプレイ保存するオプション追加（Kailleraオプション内）
 kailleraプレイ中はステートセーブ時の警告を出さないように
 kailleraプレイ中のオーバークロックに対応
 ゲーム起動時にCTRL+SHIFTを押すことでリプレイ自動保存の機能をトグル動作するように
 kailleraプレイ中はフレームスキップオートに固定に
 kailleraプレイ時は68kコアを強制的にCコアに
 ローカルプレイでは従来のPlusと同様に複数プロセスで起動するように変更（ネットプレイは単一プロセス）
 kailleraプレイ時の強制デフォルト値セットの内部処理変更
 kailleraプレイ時のアナログ入力にテスト対応
 連射状態の通知機能（現在GUIなし。iniファイルのdisp_autofire_status=0で無効）
 拡張ツールバー [ZetaMAME32]
 Kailleraプレイ時にネオジオでBIOSがUNIBIOS以外のときはEURO V2固定に
 ステートロードで8文字以上のドライバ名に対応
 時間調整レベル機能テスト実装
 kaillera時は強制的に英語のゲームリストに変更
 AVI録画機能（テスト版） [EmeraldMame]
 kailleraサーバー起動・終了機能（kailleraフォルダにkaillerasrv.exeを入れて実行）
 MAWS情報サイトで情報参照
 SE3208 CPUコアのステートセーブ対応
 Direct Inputを強制的に使用するオプション追加（GUIなし。iniファイルのforceuse_dinput=1で有効）
 Kaillera時のフェイクバージョン機能（ui\mame32ui.iniのKailleraFakeVersionを"0.119 (Sep 17 2007)"等に設定して使用）
 UNIBIOSの旧バージョンをサポート
 自動音量調整機能の復活



cps1.c
 キャプテンコマンドー 3/4 Players for kaillera追加


cps2.c
 D&D SOM 4 Players for kaillera追加
 バトルサーキット 4 Players for kaillera追加
 エイリアンVSプレデター 3 Players for kaillera追加
 Street Fighter Alpha 3 (US 980904 / Battle Modes Unlocked)追加
 Street Fighter Alpha 3 (US 980629 / Battle Modes Unlocked)追加
 Street Fighter Zero 3 (Japan 980904 / Battle Modes Unlocked)追加
 Street Fighter Zero 3 (Japan 980727 / Battle Modes Unlocked)追加
 Street Fighter Zero 3 (Japan 980629 / Battle Modes Unlocked)追加
 Street Fighter Zero 3 (Asia 980701 / Battle Modes Unlocked)追加
 X-Men Vs. Street Fighter (region changed to Japan)追加
 X-Men Vs. Street Fighter (4 Players for kaillera)追加
 Marvel Super Heroes Vs. Street Fighter (Japan 970707 / 4 Players for kaillera)追加
 Marvel Vs. Capcom: Clash of Super Heroes (Japan 980123 / 4 Players for kaillera)追加
 1944: The Loop Master (region changed to Japan)追加
 ハパ2のNVRAM初期値を日本、スピードセレクトフリーに変更


cps3.c
 リージョンハック追加（ディップスイッチで変更）
 隠しキャラクター有効版のジョジョの奇妙な冒険 -未来への遺産-追加


ddenlovr.c
 内部時計の処理変更


fcrash.c
 Final Crashのコンティニュー画面修正


fromanc2.c
 Taisen Idol-Mahjong Final Romance 2 (Japan / kaillera)追加
 Taisen Mahjong Final Romance R (Japan / kaillera)追加
 Taisen Mahjong Final Romance 4 (Japan / kaillera)追加


fuukifg3.c
 アシュラブレードとアシュラバスターにスピードアップハック追加


hyperspt.c
 Hyper Sports (4 Players for kaillera)追加
 Hyper Olympic '84 (4 Players for kaillera)追加


macrossp.c
 クイズ！！美少女戦士セーラームーンの音量バランス調整


namcos86.c
 The Return of Ishtar (2 Players for kaillera)追加


neogeo.c
 内部時計の処理変更
 Kailleraプレイ時もネオジオのセレクト（次のゲーム、前のゲーム）ボタン使用可に
 ネオジオのリセット処理の改良と地域のデフォルトを日本に
 勝負あり、バサラノイズ修正パッチ適用済みMame Plus! orzのセットssh5spnd追加
 リーグボウリング 4 Players for kaillera追加
 The King of Fighters '95 (6 Players for kaillera)追加
 The King of Fighters '98 (6 Players for kaillera)追加
 メタルスラッグシリーズの初期設定をオーバークロック200%状態に


ninjaw.c
 ダライアスⅡの音量バランス調整


pgm.c
 Knights of Valour 2 (ver. 106 / 4 Players for kaillera)追加
 Knights of Valour 2 Plus - Nine Dragons (ver. M204XX / 4 Players for kaillera)追加


psikyo4.c
 Taisen Hot Gimmick (Japan / kaillera)追加
 Taisen Hot Gimmick Kairakuten (Japan / kaillera)追加
 Taisen Hot Gimmick 3 Digital Surfing (Japan / kaillera)追加
 Quiz de Idol! Hot Debut (Japan / kaillera)追加
 Lode Runner - The Dig Fight (ver. B) (Japan / co kaillera)追加
 Lode Runner - The Dig Fight (ver. B) (Japan / vs kaillera)追加
 Load Runnerのディップ初期値を日本に変更
 Taisen Hot Gimmick 4 Ever (Japan / kaillera)追加

segas16c.c（ドライバ追加）
 Fantasy Zone II DX追加

segas32.c
 Golden Axe: The Revenge of Death Adder (Japan / 4 Players for kaillera)追加


seibuspi.c
 kailleraプレイ時でもnvramを使用


suprnova.c
 内部時計の処理変更


taito_f3.c
 Arabian Magic (World / 4 Players for kaillera)追加
 Arabian Magic (Japan / 4 Players for kaillera)追加
 Arabian Magic (US / 4 Players for kaillera)追加
 Dungeon Magic (World / 4 Players for kaillera)追加（オーバークロック200%）
 Light Bringer (Japan / 4 Players for kaillera)追加（オーバークロック200%）
 Dungeon Magic (US / 4 Players for kaillera)追加（オーバークロック200%）


trackfld.c
 Hyper Olympic (4 Players for kaillera)追加


warriorb.c
 ダライアスⅡの音量バランス調整



既知の不具合
　ネットゲーム終了後、別のゲームをネットプレイすると突然本体ごと落ちる場合がある（プレイ毎にエミュ本体再起動推奨）




《ui.bdfの作成方法》
http://crl.nmsu.edu/~mleisher/ttf2bdf.html
上記のURLよりotf2bdfを入手
コマンドプロンプトを開いて下記のコマンドを実行すればMSゴシックを変換します。

otf2bdf.exe %windir%\Fonts\msgothic.ttc -o ui.bdf -p 24
