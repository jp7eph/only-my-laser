#include <M5StickC.h>
#include <BleKeyboard.h>

#define TIME_BTNB_LONG_PRESS 1000  // ボタンBの長押しと判定する時間(ms)

#define LCD_TOP_OFFSET 5  // LCD上部が被って見えないから、オフセット

#define ALARM_VIN_VOLTAGE 3.5  // 外部電源電圧[V]の警告表示のしきい値
#define ALARM_BAT_LEVEL 25     // 内蔵バッテリーレベル[%]の警告表示のしきい値

hw_timer_t * presenTimer = NULL;

BleKeyboard bleKeyboard("only-my-laser");

bool FlagBleConnected = false;  // Bluetoothの接続判定フラグ。接続済:true / 未接続:false
unsigned long nextMonitorMills = 0;  // 次回のモニタを更新するミリ秒を格納する

int GetBatLevel(void);

void setup() {
    M5.begin();
    bleKeyboard.begin();
    presenTimer = timerBegin(0, 80, true);
    // monitorTimer = timerBegin(1, 80, true);

    setCpuFrequencyMhz(80);
    M5.Axp.ScreenBreath(9);
    M5.Lcd.fillScreen(BLACK);

    // presenTimer 初期停止と初期化
    timerStop(presenTimer);
    timerWrite(presenTimer, 0);

    bleKeyboard.setBatteryLevel(GetBatLevel());

    Serial.println("Setup() done");  // DEBUG
}

void loop() {
    // ボタン状態更新
    M5.update();

    /* -- モニタ関連 -- */
    // 更新周期を1秒間隔にする
    if ( nextMonitorMills < millis() ){
        M5.Lcd.setTextSize(1);
        // 外部電源(+5V)の電圧表示 
        M5.Lcd.setCursor(0,0 + LCD_TOP_OFFSET);
        float vinVoltage = M5.Axp.GetVinVoltage();
        M5.Lcd.printf("Ext: ");
        // 外部電源がしきい値(ALARM_VIN_VOLTAGE)を下回ったら、警告表示
        if ( vinVoltage < ALARM_VIN_VOLTAGE ){
            M5.Lcd.setTextColor(WHITE, RED);
            M5.Lcd.printf("%.2fV", vinVoltage);
            M5.Lcd.setTextColor(WHITE, BLACK);
        } else {
            M5.Lcd.printf("%.2fV", vinVoltage);
        }
        // 内蔵バッテリーの残量レベル表示
        M5.Lcd.setCursor(0,10 + LCD_TOP_OFFSET);
        M5.Lcd.printf("Bat: "); 
        // 内蔵バッテリーの残量がしきい値(ALARM_BAT_LEVEL)を下回ったら、警告表示
        if ( GetBatLevel() < ALARM_BAT_LEVEL ){
            M5.Lcd.setTextColor(WHITE, RED);
            M5.Lcd.printf("%3d%%", GetBatLevel());
            M5.Lcd.setTextColor(WHITE, BLACK);
        } else {
            M5.Lcd.printf("%3d%%", GetBatLevel());
        }
        // 内蔵バッテリーの充放電表示
        // +なら充電(+)、-なら放電(-)
        M5.Lcd.setCursor(65,10 + LCD_TOP_OFFSET);
        if ( M5.Axp.GetBatCurrent() == 0.0 ){
            M5.Lcd.printf("(0");
        } else if ( M5.Axp.GetBatCurrent() > 0.0 ){
            M5.Lcd.printf("(+");
        } else if ( M5.Axp.GetBatCurrent() < 0.0 ){
            M5.Lcd.printf("(-");
        }
        // Bluetooth接続状態の表示
        M5.Lcd.setCursor(0, 20 + LCD_TOP_OFFSET);
        M5.Lcd.printf("Con: ");
        if ( FlagBleConnected ){
            M5.Lcd.printf("O");
        } else {
            M5.Lcd.setTextColor(WHITE, RED);
            M5.Lcd.printf("X");
            M5.Lcd.setTextColor(WHITE, BLACK);
        }
        // APS電圧の低電圧警告の表示
        // APS電圧: AXP192からESP32に給電される電圧。3.4V以下はWarning。
        // https://lang-ship.com/reference/unofficial/M5StickC/Class/AXP192/#getwarningleve
        if ( M5.Axp.GetWarningLevel() != 0 ){
            M5.Lcd.setTextColor(WHITE, RED);
            M5.Lcd.setCursor(0, 30 + LCD_TOP_OFFSET);
            M5.Lcd.printf("LOW VOLTAGE!!");
            M5.Lcd.setTextColor(WHITE, BLACK);
        } else {
            M5.Lcd.setCursor(0, 30 + LCD_TOP_OFFSET);
            M5.Lcd.printf("%*s", 13, "");
        }
        nextMonitorMills = millis() + 1000;
    }
    
    /* -- タイマー関連 -- */
    // 電源ボタンの押下状態を取得する。
    // HACK: GetBtnPress()は0以外の値は一度しか値を取得できないから、ここで取得する。
    //       格納しないで下のif文で呼ぶと、ボタンを長押ししても[1]ではなく、先に短い場合の[2]を返してしまうため、長押しの分岐に入らない。
    int axpBtn = M5.Axp.GetBtnPress();
    // 電源ボタンが短時間押されたときは、タイマーのStart/Stop
    if( axpBtn == 2 ){
        if ( timerStarted(presenTimer) ){
            timerStop(presenTimer);
        } else {
            timerStart(presenTimer);
        }
    }
    // 電源ボタンが長時間(1.5s)押されたら、タイマーリセット
    if ( axpBtn == 1 ){
        timerStop(presenTimer);
        timerWrite(presenTimer, 0);
    }
    int presenTimerSec = int(timerReadSeconds(presenTimer));
    // 表示部分
    // 分は大きく
    M5.Lcd.setTextSize(3);
    M5.Lcd.setCursor(0,70);
    M5.Lcd.printf("%2d", presenTimerSec / 60);
    // 秒は小さく
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(50,70);
    M5.Lcd.printf("%02d", presenTimerSec % 60);

    /* -- スライド操作関連 -- */
    // Bluetooth接続後の動作
    if( bleKeyboard.isConnected() ){
        FlagBleConnected = true;
        // ボタンAが押されたとき、[n]を送信
        if( M5.BtnA.wasReleased() ){
            Serial.println("Send n key");
            bleKeyboard.print("n");
        }
        // ボタンBが押されたとき、[p]を送信
        if( M5.BtnB.wasReleased() ){
            Serial.println("Send p key");
            bleKeyboard.print("p");
        }
        // ボタンBが指定秒数以上押されたとき、[b]を送信
        if( M5.BtnB.wasReleasefor(TIME_BTNB_LONG_PRESS) ){
            Serial.println("Send b key");
            bleKeyboard.print("b");
        }
        bleKeyboard.releaseAll();
    } else {
        FlagBleConnected = false;
    }

    /* -- ボタン説明表示 -- */
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(0,120);
    M5.Lcd.printf("F: >");
    M5.Lcd.setCursor(0,130);
    M5.Lcd.printf("R: < / Black");
    M5.Lcd.setCursor(0,140);
    // プレゼンタイマーの動作によってStart/Stopの表示を切り替える
    if ( timerStarted(presenTimer) ){
        M5.Lcd.printf("L: Stop ");
    } else {
        M5.Lcd.printf("L: Start");
    }
    M5.Lcd.setCursor(0,150);
    M5.Lcd.printf("   Reset");

    delay(100);
}

// バッテリー残量レベル表示関数
// return: int バッテリーレベル(0〜100)
// 0% / 100%とする電圧値は関数内のローカル変数で定義。
int GetBatLevel(void) {
    float v = 0.0;   // 電圧
    int   batlevel = 0; // レベル

    float maxVoltage = 4.1;  // 100%とする電圧値。バッテリーの個体差があるから、適宜変える。
    float minVoltage = 3.0;   // 0%とする電圧値。
    
    v = M5.Axp.GetBatVoltage();
    v = int((v - minVoltage) / (maxVoltage - minVoltage) * 100);
    if ( v > 100 ) {
        v = 100;
    } else if ( v < 0 ) {
        v = 0;
    }
    return v; 
}