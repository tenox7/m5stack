#include <M5Cardputer.h>

M5Canvas canvas(&M5Cardputer.Display);

const int GROUND_Y = 120;

struct Player {
    float x;
    int health;
    bool facingRight;
    int action;
    int actionTimer;
    bool hit;
};

Player p1, p2;

void resetGame() {
    p1 = {60, 100, true, 0, 0, false};
    p2 = {180, 100, false, 0, 0, false};
}

void drawStickFigure(int x, int y, bool facingRight, int action, uint16_t color, bool dead) {
    int dir = facingRight ? 1 : -1;

    if (dead) {
        canvas.drawCircle(x - dir * 20, y - 5, 5, color);
        canvas.drawLine(x - dir * 15, y - 5, x + dir * 5, y - 5, color);
        canvas.drawLine(x + dir * 5, y - 5, x + dir * 15, y - 10, color);
        canvas.drawLine(x + dir * 5, y - 5, x + dir * 15, y, color);
        canvas.drawLine(x - dir * 10, y - 5, x - dir * 5, y - 12, color);
        canvas.drawLine(x - dir * 10, y - 5, x - dir * 5, y + 2, color);
        return;
    }

    canvas.drawCircle(x, y - 35, 5, color);
    canvas.drawLine(x, y - 30, x, y - 10, color);

    if (action == 2) {
        canvas.drawLine(x, y - 10, x, y, color);
        canvas.drawLine(x, y - 10, x + dir * 15, y - 10, color);
    } else {
        canvas.drawLine(x, y - 10, x - 8, y, color);
        canvas.drawLine(x, y - 10, x + 8, y, color);
    }

    if (action == 1) {
        canvas.drawLine(x, y - 25, x + dir * 15, y - 25, color);
        canvas.drawLine(x, y - 25, x - dir * 8, y - 20, color);
    } else if (action == 2) {
        canvas.drawLine(x, y - 25, x - 8, y - 20, color);
        canvas.drawLine(x, y - 25, x + 8, y - 20, color);
    } else if (action == 3) {
        canvas.drawLine(x, y - 25, x + dir * 5, y - 30, color);
        canvas.drawLine(x, y - 25, x + dir * 5, y - 15, color);
        canvas.drawLine(x + dir * 5, y - 30, x + dir * 5, y - 15, color);
    } else {
        canvas.drawLine(x, y - 25, x - 8, y - 18, color);
        canvas.drawLine(x, y - 25, x + 8, y - 18, color);
    }
}

void drawHealthBar(int x, int health, uint16_t color) {
    canvas.drawRect(x, 5, 52, 8, TFT_WHITE);
    canvas.fillRect(x + 1, 6, health / 2, 6, color);
}

bool checkHit(Player& attacker, Player& defender, int range) {
    if (defender.action == 3) return false;
    float dist = attacker.facingRight ? (defender.x - attacker.x) : (attacker.x - defender.x);
    return dist > 0 && dist < range;
}

void updatePlayer(Player& p, Player& opponent, bool left, bool right, bool hit, bool kick, bool block) {
    if (p.actionTimer > 0) {
        p.actionTimer--;
        if (p.actionTimer == 0) p.action = 0;
        return;
    }

    if (block) {
        p.action = 3;
    } else if (hit) {
        p.action = 1;
        p.actionTimer = 10;
        if (checkHit(p, opponent, 25)) {
            opponent.health -= 5;
            opponent.hit = true;
        }
    } else if (kick) {
        p.action = 2;
        p.actionTimer = 15;
        if (checkHit(p, opponent, 30)) {
            opponent.health -= 8;
            opponent.hit = true;
        }
    } else {
        p.action = 0;
        if (left && p.x > 20) p.x -= 2;
        if (right && p.x < 220) p.x += 2;
    }
    p.facingRight = p.x < opponent.x;
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    canvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
    resetGame();
}

void loop() {
    M5Cardputer.update();

    bool p1Left = M5Cardputer.Keyboard.isKeyPressed('a');
    bool p1Right = M5Cardputer.Keyboard.isKeyPressed('d');
    bool p1Hit = M5Cardputer.Keyboard.isKeyPressed('w');
    bool p1Kick = M5Cardputer.Keyboard.isKeyPressed('s');
    bool p1Block = M5Cardputer.Keyboard.isKeyPressed('q');

    bool p2Left = M5Cardputer.Keyboard.isKeyPressed('j');
    bool p2Right = M5Cardputer.Keyboard.isKeyPressed('l');
    bool p2Hit = M5Cardputer.Keyboard.isKeyPressed('i');
    bool p2Kick = M5Cardputer.Keyboard.isKeyPressed('k');
    bool p2Block = M5Cardputer.Keyboard.isKeyPressed('u');

    updatePlayer(p1, p2, p1Left, p1Right, p1Hit, p1Kick, p1Block);
    updatePlayer(p2, p1, p2Left, p2Right, p2Hit, p2Kick, p2Block);

    canvas.fillSprite(TFT_BLACK);
    canvas.drawFastHLine(0, GROUND_Y, 240, TFT_DARKGREY);

    drawHealthBar(10, p1.health, TFT_CYAN);
    drawHealthBar(178, p2.health, TFT_ORANGE);

    canvas.setTextSize(1);
    canvas.setTextColor(TFT_WHITE);
    canvas.drawString("P1:WASD+Q", 5, 15);
    canvas.drawString("P2:IJKL+U", 175, 15);

    uint16_t p1Color = p1.hit ? TFT_RED : TFT_CYAN;
    uint16_t p2Color = p2.hit ? TFT_RED : TFT_ORANGE;
    p1.hit = p2.hit = false;

    drawStickFigure(p1.x, GROUND_Y, p1.facingRight, p1.action, p1Color, p1.health <= 0);
    drawStickFigure(p2.x, GROUND_Y, p2.facingRight, p2.action, p2Color, p2.health <= 0);

    if (p1.health <= 0 || p2.health <= 0) {
        canvas.setTextSize(2);
        canvas.drawCentreString(p1.health <= 0 ? "P2 WINS!" : "P1 WINS!", 120, 50, 1);
        canvas.setTextSize(1);
        canvas.drawCentreString("Press ENTER to restart", 120, 70, 1);
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) resetGame();
    }

    canvas.pushSprite(0, 0);
    delay(16);
}
