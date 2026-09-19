#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <math.h>

// ============================================================
// ForgeUI MicroPilot
// ESP32-S3 + ST7789 240x240 + Analog Joystick
// Miniature animated Primary Flight Display
// ============================================================

// ---------------- Display -----------------------------------

#define TFT_SCLK 12
#define TFT_MOSI 11
#define TFT_DC    9
#define TFT_RST   10
#define TFT_CS    8

#define SCREEN_W 240
#define SCREEN_H 240

Arduino_DataBus *bus = new Arduino_ESP32SPI(
    TFT_DC,
    TFT_CS,
    TFT_SCLK,
    TFT_MOSI,
    GFX_NOT_DEFINED,
    HSPI
);

Arduino_GFX *gfx = new Arduino_ST7789(
    bus,
    TFT_RST,
    0,
    true,
    SCREEN_W,
    SCREEN_H
);

// Full-screen off-screen canvas
Arduino_Canvas *canvas = nullptr;

// ---------------- Joystick ----------------------------------

constexpr int JOY_X  = 6;
constexpr int JOY_Y  = 5;
constexpr int JOY_SW = 4;

int joyCentreX = 2048;
int joyCentreY = 2048;

// ---------------- Colours -----------------------------------

constexpr uint16_t C_BLACK      = 0x0000;
constexpr uint16_t C_WHITE      = 0xFFFF;
constexpr uint16_t C_CYAN       = 0x07FF;
constexpr uint16_t C_BLUE       = 0x001F;
constexpr uint16_t C_GREEN      = 0x07E0;
constexpr uint16_t C_YELLOW     = 0xFFE0;
constexpr uint16_t C_RED        = 0xF800;
constexpr uint16_t C_GREY       = 0x8410;
constexpr uint16_t C_DKGREY     = 0x3186;

// PFD colours
constexpr uint16_t SKY_BLUE     = 0x249F;
constexpr uint16_t SKY_DARK     = 0x1275;
constexpr uint16_t GROUND_BROWN = 0x8A82;
constexpr uint16_t GROUND_DARK  = 0x5140;
constexpr uint16_t PFD_MAGENTA  = 0xF81F;

// ---------------- Simulation --------------------------------

float bankDeg = 0.0f;
float pitchDeg = 0.0f;

float commandedBank = 0.0f;
float commandedPitch = 0.0f;

float airspeed = 105.0f;
float altitude = 2450.0f;
float verticalSpeed = 0.0f;
float heading = 270.0f;

bool autopilot = false;

unsigned long lastFrame = 0;
unsigned long lastButtonTime = 0;

// ============================================================
// Helpers
// ============================================================

float degToRad(float deg)
{
    return deg * 0.01745329252f;
}

float clampFloat(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float smoothToward(float current, float target, float amount)
{
    return current + (target - current) * amount;
}

bool buttonPressed()
{
    return digitalRead(JOY_SW) == LOW;
}

float readAxis(int raw, int centre)
{
    constexpr int deadZone = 180;

    int delta = raw - centre;

    if (abs(delta) < deadZone)
        return 0.0f;

    float value = 0.0f;

    if (delta > 0)
    {
        int range = 4095 - centre - deadZone;

        if (range > 0)
            value =
                (float)(delta - deadZone) /
                (float)range;
    }
    else
    {
        int range = centre - deadZone;

        if (range > 0)
            value =
                (float)(delta + deadZone) /
                (float)range;
    }

    return clampFloat(value, -1.0f, 1.0f);
}

// Rotate a point around screen centre.
//
// lx / ly = coordinates relative to PFD centre.
// pitchOffset is applied before rotation.
void rotatePoint(
    float lx,
    float ly,
    float angle,
    int &sx,
    int &sy)
{
    float c = cosf(angle);
    float s = sinf(angle);

    sx =
        (int)(
            SCREEN_W / 2 +
            lx * c -
            ly * s
        );

    sy =
        (int)(
            SCREEN_H / 2 +
            lx * s +
            ly * c
        );
}

// ============================================================
// Text helpers
// ============================================================

void centredText(
    const char *text,
    int y,
    int size,
    uint16_t colour)
{
    canvas->setTextSize(size);
    canvas->setTextColor(colour);

    int16_t x1;
    int16_t y1;
    uint16_t w;
    uint16_t h;

    canvas->getTextBounds(
        text,
        0,
        0,
        &x1,
        &y1,
        &w,
        &h
    );

    canvas->setCursor(
        (SCREEN_W - w) / 2,
        y
    );

    canvas->print(text);
}

// ============================================================
// Joystick calibration
// ============================================================

void calibrateJoystick()
{
    canvas->fillScreen(C_BLACK);

    centredText(
        "FORGEUI",
        64,
        3,
        C_CYAN
    );

    centredText(
        "MICROPILOT",
        98,
        2,
        C_WHITE
    );

    centredText(
        "FLIGHT CONTROL",
        132,
        1,
        C_GREY
    );

    centredText(
        "CALIBRATING...",
        154,
        1,
        C_GREEN
    );

    centredText(
        "RELEASE STICK",
        178,
        1,
        C_YELLOW
    );

    canvas->flush();

    long totalX = 0;
    long totalY = 0;

    constexpr int samples = 64;

    for (int i = 0; i < samples; i++)
    {
        totalX += analogRead(JOY_X);
        totalY += analogRead(JOY_Y);
        delay(5);
    }

    joyCentreX = totalX / samples;
    joyCentreY = totalY / samples;

    Serial.printf(
        "Joystick centre X=%d Y=%d\n",
        joyCentreX,
        joyCentreY
    );
}

// ============================================================
// Simulation
// ============================================================

void updateFlightModel()
{
    float joyX =
        readAxis(
            analogRead(JOY_X),
            joyCentreX
        );

    float joyY =
        readAxis(
            analogRead(JOY_Y),
            joyCentreY
        );

    if (autopilot)
    {
        // Wings level / zero pitch.
        commandedBank = 0.0f;
        commandedPitch = 0.0f;
    }
    else
    {
        commandedBank =
            joyX * 55.0f;

        // Physical joystick forward is normally negative ADC delta.
        commandedPitch =
            -joyY * 25.0f;
    }

    // Smooth aircraft response.
    bankDeg =
        smoothToward(
            bankDeg,
            commandedBank,
            autopilot ? 0.075f : 0.10f
        );

    pitchDeg =
        smoothToward(
            pitchDeg,
            commandedPitch,
            autopilot ? 0.065f : 0.085f
        );

    // Simulated heading changes with bank.
    heading += bankDeg * 0.0025f;

    while (heading >= 360.0f)
        heading -= 360.0f;

    while (heading < 0.0f)
        heading += 360.0f;

    // Vertical speed responds to pitch.
    float targetVS =
        pitchDeg * 65.0f;

    verticalSpeed =
        smoothToward(
            verticalSpeed,
            targetVS,
            0.035f
        );

    altitude +=
        verticalSpeed / 1800.0f;

    if (altitude < 0)
        altitude = 0;

    // Airspeed changes gently with pitch.
    float targetSpeed =
        110.0f -
        pitchDeg * 0.45f;

    targetSpeed =
        clampFloat(
            targetSpeed,
            65.0f,
            165.0f
        );

    airspeed =
        smoothToward(
            airspeed,
            targetSpeed,
            0.025f
        );
}

// ============================================================
// Horizon background
// ============================================================

void drawHorizonBackground()
{
    // First draw full sky.
    canvas->fillScreen(SKY_BLUE);

    float bank =
        degToRad(bankDeg);

    // Pitch scale:
    // 3 pixels per degree gives ±25 degrees plenty of movement.
    float pitchPixels =
        pitchDeg * 3.0f;

    // Horizon line equation in screen space.
    //
    // Instead of rotating an enormous bitmap, calculate the horizon
    // Y position for each screen column and fill ground below it.
    float slope =
        tanf(bank);

    for (int x = 0; x < SCREEN_W; x++)
    {
        float relativeX =
            x - SCREEN_W / 2.0f;

        float horizonY =
            SCREEN_H / 2.0f +
            pitchPixels +
            relativeX * slope;

        int y =
            (int)horizonY;

        if (y < 0)
        {
            canvas->drawFastVLine(
                x,
                0,
                SCREEN_H,
                GROUND_BROWN
            );
        }
        else if (y < SCREEN_H)
        {
            canvas->drawFastVLine(
                x,
                y,
                SCREEN_H - y,
                GROUND_BROWN
            );
        }
    }

    // Horizon itself.
    int hx1;
    int hy1;
    int hx2;
    int hy2;

    rotatePoint(
        -170,
        pitchPixels,
        bank,
        hx1,
        hy1
    );

    rotatePoint(
        170,
        pitchPixels,
        bank,
        hx2,
        hy2
    );

    canvas->drawLine(
        hx1,
        hy1,
        hx2,
        hy2,
        C_WHITE
    );
}

// ============================================================
// Pitch ladder
// ============================================================

void drawPitchLadder()
{
    float bank =
        degToRad(bankDeg);

    constexpr float pixelsPerDegree = 3.0f;

    // Draw -30 to +30 degree ladder.
    for (int mark = -30; mark <= 30; mark += 5)
    {
        if (mark == 0)
            continue;

        float localY =
            (pitchDeg - mark) *
            pixelsPerDegree;

        int halfWidth =
            (mark % 10 == 0)
                ? 25
                : 14;

        int x1;
        int y1;
        int x2;
        int y2;

        rotatePoint(
            -halfWidth,
            localY,
            bank,
            x1,
            y1
        );

        rotatePoint(
            halfWidth,
            localY,
            bank,
            x2,
            y2
        );

        // Skip lines that are completely well outside display.
        if ((y1 < -20 && y2 < -20) ||
            (y1 > SCREEN_H + 20 &&
             y2 > SCREEN_H + 20))
        {
            continue;
        }

        canvas->drawLine(
            x1,
            y1,
            x2,
            y2,
            C_WHITE
        );

        // Small end ticks.
        float tickDirection =
            mark > 0
                ? 4.0f
                : -4.0f;

        int tx1;
        int ty1;
        int tx2;
        int ty2;

        rotatePoint(
            -halfWidth,
            localY + tickDirection,
            bank,
            tx1,
            ty1
        );

        rotatePoint(
            halfWidth,
            localY + tickDirection,
            bank,
            tx2,
            ty2
        );

        canvas->drawLine(
            x1,
            y1,
            tx1,
            ty1,
            C_WHITE
        );

        canvas->drawLine(
            x2,
            y2,
            tx2,
            ty2,
            C_WHITE
        );
    }
}

// ============================================================
// Fixed aircraft symbol
// ============================================================

void drawAircraftSymbol()
{
    constexpr int cx = SCREEN_W / 2;
    constexpr int cy = SCREEN_H / 2;

    // Black outline for readability.
    canvas->drawFastHLine(
        cx - 42,
        cy,
        28,
        C_BLACK
    );

    canvas->drawFastHLine(
        cx + 14,
        cy,
        28,
        C_BLACK
    );

    // Yellow wings.
    canvas->drawFastHLine(
        cx - 40,
        cy,
        26,
        C_YELLOW
    );

    canvas->drawFastHLine(
        cx + 14,
        cy,
        26,
        C_YELLOW
    );

    canvas->drawFastVLine(
        cx - 14,
        cy,
        7,
        C_YELLOW
    );

    canvas->drawFastVLine(
        cx + 14,
        cy,
        7,
        C_YELLOW
    );

    // Centre reference.
    canvas->drawCircle(
        cx,
        cy,
        4,
        C_YELLOW
    );

    canvas->fillCircle(
        cx,
        cy,
        1,
        C_WHITE
    );
}
// ============================================================
// Roll scale
// ============================================================

void drawRollScale()
{
    constexpr int cx = SCREEN_W / 2;
    constexpr int cy = 92;
    constexpr int radius = 74;

    // Fixed roll marks across the top.
    const int marks[] =
    {
        -60, -45, -30, -20, -10,
         0,
         10, 20, 30, 45, 60
    };

    constexpr int markCount =
        sizeof(marks) / sizeof(marks[0]);

    for (int i = 0; i < markCount; i++)
    {
        float angle =
            degToRad(
                marks[i] - 90.0f
            );

        int innerRadius =
            (marks[i] % 30 == 0)
                ? radius - 9
                : radius - 5;

        int x1 =
            cx +
            cosf(angle) *
            innerRadius;

        int y1 =
            cy +
            sinf(angle) *
            innerRadius;

        int x2 =
            cx +
            cosf(angle) *
            radius;

        int y2 =
            cy +
            sinf(angle) *
            radius;

        canvas->drawLine(
            x1,
            y1,
            x2,
            y2,
            C_WHITE
        );
    }

    // Fixed centre triangle.
    canvas->fillTriangle(
        cx,
        cy - radius + 1,
        cx - 5,
        cy - radius + 9,
        cx + 5,
        cy - radius + 9,
        C_WHITE
    );

    // Moving bank pointer.
    float pointerAngle =
        degToRad(
            bankDeg - 90.0f
        );

    int px =
        cx +
        cosf(pointerAngle) *
        (radius - 14);

    int py =
        cy +
        sinf(pointerAngle) *
        (radius - 14);

    int lx =
        cx +
        cosf(pointerAngle - 0.07f) *
        (radius - 22);

    int ly =
        cy +
        sinf(pointerAngle - 0.07f) *
        (radius - 22);

    int rx =
        cx +
        cosf(pointerAngle + 0.07f) *
        (radius - 22);

    int ry =
        cy +
        sinf(pointerAngle + 0.07f) *
        (radius - 22);

    canvas->fillTriangle(
        px,
        py,
        lx,
        ly,
        rx,
        ry,
        C_YELLOW
    );
}

// ============================================================
// Airspeed tape
// ============================================================

void drawAirspeedTape()
{
    constexpr int x = 0;
    constexpr int y = 34;
    constexpr int w = 42;
    constexpr int h = 164;

    canvas->fillRect(
        x,
        y,
        w,
        h,
        C_BLACK
    );

    canvas->drawRect(
        x,
        y,
        w,
        h,
        C_GREY
    );

    // Moving scale.
    int centreSpeed =
        (int)airspeed;

    for (int value =
             centreSpeed - 50;
         value <=
             centreSpeed + 50;
         value += 10)
    {
        float delta =
            value - airspeed;

        int py =
            116 -
            (int)(delta * 2.0f);

        if (py < y + 5 ||
            py > y + h - 5)
        {
            continue;
        }

        int tick =
            (value % 20 == 0)
                ? 10
                : 6;

        canvas->drawFastHLine(
            w - tick,
            py,
            tick,
            C_WHITE
        );

        if (value % 20 == 0 &&
            value >= 0)
        {
            char text[8];

            snprintf(
                text,
                sizeof(text),
                "%d",
                value
            );

            canvas->setTextSize(1);
            canvas->setTextColor(C_WHITE);
            canvas->setCursor(
                3,
                py - 3
            );
            canvas->print(text);
        }
    }

    // Current airspeed box.
    canvas->fillRect(
        0,
        105,
        42,
        23,
        C_BLACK
    );

    canvas->drawRect(
        0,
        105,
        42,
        23,
        C_CYAN
    );

    char current[8];

    snprintf(
        current,
        sizeof(current),
        "%03d",
        (int)airspeed
    );

    canvas->setTextSize(2);
    canvas->setTextColor(C_WHITE);
    canvas->setCursor(
        3,
        109
    );
    canvas->print(current);

    canvas->setTextSize(1);
    canvas->setTextColor(C_CYAN);
    canvas->setCursor(
        4,
        23
    );
    canvas->print("IAS");
}

// ============================================================
// Altitude tape
// ============================================================

void drawAltitudeTape()
{
    constexpr int x = 198;
    constexpr int y = 34;
    constexpr int w = 42;
    constexpr int h = 164;

    canvas->fillRect(
        x,
        y,
        w,
        h,
        C_BLACK
    );

    canvas->drawRect(
        x,
        y,
        w,
        h,
        C_GREY
    );

    int centreAltitude =
        ((int)altitude / 100) * 100;

    for (int value =
             centreAltitude - 500;
         value <=
             centreAltitude + 500;
         value += 100)
    {
        float delta =
            value - altitude;

        int py =
            116 -
            (int)(delta * 0.20f);

        if (py < y + 5 ||
            py > y + h - 5)
        {
            continue;
        }

        int tick =
            (value % 200 == 0)
                ? 10
                : 6;

        canvas->drawFastHLine(
            x,
            py,
            tick,
            C_WHITE
        );

        if (value % 200 == 0 &&
            value >= 0)
        {
            char text[8];

            snprintf(
                text,
                sizeof(text),
                "%d",
                value
            );

            canvas->setTextSize(1);
            canvas->setTextColor(C_WHITE);

            canvas->setCursor(
                x + 12,
                py - 3
            );

            canvas->print(text);
        }
    }

    // Current altitude box.
    canvas->fillRect(
        x,
        105,
        w,
        23,
        C_BLACK
    );

    canvas->drawRect(
        x,
        105,
        w,
        23,
        C_GREEN
    );

    char current[10];

    snprintf(
        current,
        sizeof(current),
        "%04d",
        (int)altitude
    );

    canvas->setTextSize(1);
    canvas->setTextColor(C_WHITE);

    canvas->setCursor(
        x + 6,
        113
    );

    canvas->print(current);

    canvas->setTextColor(C_GREEN);
    canvas->setCursor(
        213,
        23
    );
    canvas->print("ALT");
}

// ============================================================
// Vertical speed indicator
// ============================================================

void drawVerticalSpeed()
{
    constexpr int x = 190;
    constexpr int centreY = 116;

    canvas->drawFastVLine(
        x,
        62,
        108,
        C_GREY
    );

    canvas->drawFastHLine(
        x - 4,
        centreY,
        8,
        C_WHITE
    );

    float normalized =
        clampFloat(
            verticalSpeed / 1800.0f,
            -1.0f,
            1.0f
        );

    int pointerY =
        centreY -
        (int)(normalized * 48.0f);

    canvas->fillTriangle(
        x,
        pointerY,
        x - 7,
        pointerY - 4,
        x - 7,
        pointerY + 4,
        C_GREEN
    );

    canvas->setTextSize(1);
    canvas->setTextColor(C_GREY);

    canvas->setCursor(
        181,
        51
    );
    canvas->print("+");

    canvas->setCursor(
        181,
        172
    );
    canvas->print("-");
}

// ============================================================
// Heading strip
// ============================================================

void drawHeadingStrip()
{
    constexpr int y = 202;
    constexpr int h = 38;

    canvas->fillRect(
        0,
        y,
        SCREEN_W,
        h,
        C_BLACK
    );

    canvas->drawFastHLine(
        0,
        y,
        SCREEN_W,
        C_GREY
    );

    // Heading marks every 10 degrees.
    int base =
        ((int)heading / 10) * 10;

    for (int offset = -60;
         offset <= 60;
         offset += 10)
    {
        int hdg =
            base + offset;

        while (hdg < 0)
            hdg += 360;

        while (hdg >= 360)
            hdg -= 360;

        float difference =
            (base + offset) -
            heading;

        int px =
            SCREEN_W / 2 +
            (int)(difference * 2.0f);

        if (px < 4 ||
            px > SCREEN_W - 4)
        {
            continue;
        }

        int tickHeight =
            (hdg % 30 == 0)
                ? 8
                : 4;

        canvas->drawFastVLine(
            px,
            y,
            tickHeight,
            C_WHITE
        );

        if (hdg % 30 == 0)
        {
            char label[8];

            if (hdg == 0)
                strcpy(label, "N");
            else if (hdg == 90)
                strcpy(label, "E");
            else if (hdg == 180)
                strcpy(label, "S");
            else if (hdg == 270)
                strcpy(label, "W");
            else
                snprintf(
                    label,
                    sizeof(label),
                    "%02d",
                    hdg / 10
                );

            canvas->setTextSize(1);
            canvas->setTextColor(C_WHITE);

            canvas->setCursor(
                px - 3,
                y + 10
            );

            canvas->print(label);
        }
    }

    // Heading selection box.
    canvas->fillTriangle(
        SCREEN_W / 2,
        y,
        SCREEN_W / 2 - 5,
        y + 6,
        SCREEN_W / 2 + 5,
        y + 6,
        PFD_MAGENTA
    );

    char current[8];

    snprintf(
        current,
        sizeof(current),
        "%03d",
        (int)heading
    );

    canvas->fillRect(
        101,
        220,
        38,
        18,
        C_BLACK
    );

    canvas->drawRect(
        101,
        220,
        38,
        18,
        PFD_MAGENTA
    );

    canvas->setTextSize(1);
    canvas->setTextColor(C_WHITE);
    canvas->setCursor(
        111,
        226
    );
    canvas->print(current);
}

// ============================================================
// Flight mode annunciator
// ============================================================

void drawModeAnnunciator()
{
    canvas->fillRect(
        70,
        2,
        100,
        18,
        C_BLACK
    );

    canvas->drawRect(
        70,
        2,
        100,
        18,
        autopilot
            ? C_GREEN
            : C_GREY
    );

    canvas->setTextSize(1);

    if (autopilot)
    {
        canvas->setTextColor(C_GREEN);
        canvas->setCursor(82, 7);
        canvas->print("AP  LEVEL");
    }
    else
    {
        canvas->setTextColor(C_CYAN);
        canvas->setCursor(82, 7);
        canvas->print("MANUAL FLT");
    }
}

// ============================================================
// Warning annunciations
// ============================================================

void drawWarnings()
{
    bool excessiveBank =
        fabsf(bankDeg) > 45.0f;

    bool excessivePitch =
        fabsf(pitchDeg) > 20.0f;

    if (!excessiveBank &&
        !excessivePitch)
    {
        return;
    }

    canvas->fillRect(
        64,
        177,
        112,
        18,
        C_RED
    );

    canvas->setTextSize(1);
    canvas->setTextColor(C_WHITE);

    if (excessiveBank)
    {
        canvas->setCursor(
            82,
            183
        );
        canvas->print("BANK ANGLE");
    }
    else
    {
        canvas->setCursor(
            88,
            183
        );
        canvas->print("PITCH");
    }
}
// ============================================================
// Startup / self-test
// ============================================================

void drawStartupScreen()
{
    canvas->fillScreen(C_BLACK);

    canvas->drawRect(
        8,
        8,
        224,
        224,
        C_CYAN
    );

    canvas->drawRect(
        12,
        12,
        216,
        216,
        C_BLUE
    );

    centredText(
        "FORGEUI",
        52,
        3,
        C_CYAN
    );

    centredText(
        "MICROPILOT",
        88,
        2,
        C_WHITE
    );

    centredText(
        "PRIMARY FLIGHT DISPLAY",
        120,
        1,
        C_GREY
    );

    centredText(
        "ESP32-S3 // ST7789",
        143,
        1,
        C_WHITE
    );

    centredText(
        "SYSTEM SELF TEST",
        174,
        1,
        C_YELLOW
    );

    centredText(
        "PFD READY",
        198,
        1,
        C_GREEN
    );

    canvas->flush();
}

// ============================================================
// PFD renderer
// ============================================================

void drawPFD()
{
    // Moving attitude layer.
    drawHorizonBackground();
    drawPitchLadder();

    // Fixed flight instrumentation.
    drawRollScale();
    drawAircraftSymbol();

    drawAirspeedTape();
    drawAltitudeTape();
    drawVerticalSpeed();
    drawHeadingStrip();

    drawModeAnnunciator();
    drawWarnings();

    // Small ForgeUI identity.
    canvas->fillRect(
        2,
        2,
        61,
        17,
        C_BLACK
    );

    canvas->drawRect(
        2,
        2,
        61,
        17,
        C_CYAN
    );

    canvas->setTextSize(1);
    canvas->setTextColor(C_CYAN);

    canvas->setCursor(
        8,
        7
    );

    canvas->print("FORGEUI");

    canvas->flush();
}

// ============================================================
// Autopilot button
// ============================================================

void updateAutopilotButton()
{
    static bool previousButton = false;

    bool currentButton =
        buttonPressed();

    bool pressedEdge =
        currentButton &&
        !previousButton;

    previousButton =
        currentButton;

    if (!pressedEdge)
        return;

    // Debounce.
    if (millis() - lastButtonTime < 250)
        return;

    lastButtonTime = millis();

    autopilot = !autopilot;

    Serial.printf(
        "Autopilot: %s\n",
        autopilot
            ? "ON"
            : "OFF"
    );
}

// ============================================================
// Setup
// ============================================================

void setup()
{
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println("==============================");
    Serial.println("FORGEUI MICROPILOT");
    Serial.println("PRIMARY FLIGHT DISPLAY");
    Serial.println("ESP32-S3 + ST7789 240x240");
    Serial.println("JOY X=6 Y=5 SW=4");
    Serial.println("==============================");

    pinMode(
        JOY_SW,
        INPUT_PULLUP
    );

    analogReadResolution(12);

    // Physically proven ST7789 square-display baseline.
    if (!gfx->begin())
    {
        Serial.println(
            "DISPLAY INIT FAILED"
        );

        while (true)
            delay(1000);
    }

    // Full-resolution off-screen canvas.
    canvas =
        new Arduino_Canvas(
            SCREEN_W,
            SCREEN_H,
            gfx
        );

    if (canvas == nullptr ||
        !canvas->begin())
    {
        Serial.println(
            "CANVAS INIT FAILED"
        );

        while (true)
            delay(1000);
    }

    drawStartupScreen();

    delay(1600);

    calibrateJoystick();

    delay(500);

    // Initial simulated flight condition.
    bankDeg = 0.0f;
    pitchDeg = 0.0f;

    commandedBank = 0.0f;
    commandedPitch = 0.0f;

    airspeed = 105.0f;
    altitude = 2450.0f;
    verticalSpeed = 0.0f;
    heading = 270.0f;

    autopilot = false;

    Serial.println(
        "MICROPILOT PFD READY"
    );

    // Prevent held switch during calibration from toggling AP.
    while (buttonPressed())
        delay(10);
}

// ============================================================
// Main loop
// ============================================================

void loop()
{
    // Approximately 30 FPS.
    if (millis() - lastFrame < 33)
        return;

    lastFrame = millis();

    updateAutopilotButton();

    updateFlightModel();

    drawPFD();
}
