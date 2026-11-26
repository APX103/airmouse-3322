/* Host simulation of the AirMouse button scanner (feat/center-key).
 *
 * Compiles the REAL button/input source (btn_slice.inc, extracted from
 * 0001-sle-sample-airmouse.patch by extract.py) against stub GPIO, then walks a
 * deterministic press/release/bounce timeline and checks the keys bitmap and
 * the 2-tick debounce state machine.
 *
 * Pin map (product_pin.h: AGPIO_n = 75 + n):
 *   L = S_AGPIO20 (95), R = S_AGPIO12 (87), M = S_AGPIO17 (92, onboard SW2102)
 *
 * Level convention per test stage: 1 = released (idle), 0 = pressed, matching
 * the ground-style wiring; stage 9 re-inits with idle-low pins to prove the
 * open-level adaptivity ("电平与开路电平相反 = 按下" works both polarities).
 */
#include <stdint.h>
#include <stdio.h>

/* ---------------- stubs: GPIO / OS ---------------- */
typedef int pin_t;
enum { S_AGPIO12 = 87, S_AGPIO17 = 92, S_AGPIO20 = 95 };
typedef enum { PIN_PULL_NONE, PIN_PULL_DOWN, PIN_PULL_UP } pin_pull_t;
enum { PIN_MODE_0 = 0 };
typedef enum { GPIO_DIRECTION_INPUT = 0 } gpio_direction_t;
typedef enum { GPIO_LEVEL_LOW = 0, GPIO_LEVEL_HIGH = 1 } gpio_level_t;

static int g_level[128]; /* 1 = high */
static int g_mslept;     /* 累计"睡过"的毫秒数，用于断言开机采样窗 */

static gpio_level_t uapi_gpio_get_val(pin_t pin) { return g_level[pin] ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW; }
static void uapi_pin_set_mode(pin_t pin, int mode) { (void)pin; (void)mode; }
static void uapi_pin_set_pull(pin_t pin, pin_pull_t pull) { (void)pin; (void)pull; }
static void uapi_gpio_set_dir(pin_t pin, gpio_direction_t dir) { (void)pin; (void)dir; }
static void osal_msleep(int ms) { g_mslept += ms; }

/* 固件里的统计打印在仿真里静默 */
#define printf(...) ((void)0)

#include "btn_slice.inc"

/* ---------------- test harness ---------------- */
static int g_pass, g_fail;

#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "[btn_sim:%d] FAIL: %s\n", __LINE__, name); } \
} while (0)

static void set_all(int l, int r, int m) { g_level[S_AGPIO20] = l; g_level[S_AGPIO12] = r; g_level[S_AGPIO17] = m; }

int main(void)
{
    /* 1. 上电：三键全部空闲高（接地式接线的常态） */
    set_all(1, 1, 1);
    g_mslept = 0;
    aim_btn_init();
    CHECK(g_mslept >= 120, "开机采样窗 >= 120ms");
    CHECK(g_aim_btn_open[0] == 1 && g_aim_btn_open[1] == 1 && g_aim_btn_open[2] == 1,
        "开路电平 = 111");

    /* 2. 空闲扫描稳定输出 0 */
    CHECK(aim_btn_scan() == 0, "空闲 scan=0 #1");
    CHECK(aim_btn_scan() == 0, "空闲 scan=0 #2");

    /* 3. M 键触点抖动：电平来回跳，永不满足"两拍一致" */
    int saw_zero = 1;
    for (int i = 0; i < 6; i++) {
        g_level[S_AGPIO17] = (i & 1) ? 0 : 1;
        if (aim_btn_scan() != 0) { saw_zero = 0; }
    }
    CHECK(saw_zero, "抖动期间 scan 恒 0");
    g_level[S_AGPIO17] = 1;

    /* 4. M 键正常按下：第一拍变化不传（消抖），第二拍起报 0x04 */
    g_level[S_AGPIO17] = 0;
    CHECK(aim_btn_scan() == 0, "M 按下第1拍仍 0（消抖）");
    CHECK(aim_btn_scan() == 0x04, "M 按下第2拍=0x04");
    CHECK(aim_btn_scan() == 0x04, "M 保持=0x04");
    CHECK(aim_btn_scan() == 0x04, "M 保持=0x04（连续）");

    /* 5. M 键松开：同样要两拍 */
    g_level[S_AGPIO17] = 1;
    CHECK(aim_btn_scan() == 0x04, "M 松开第1拍仍 0x04（消抖）");
    CHECK(aim_btn_scan() == 0, "M 松开第2拍=0");

    /* 6. L+R 同按 -> 0x03 */
    g_level[S_AGPIO20] = 0;
    g_level[S_AGPIO12] = 0;
    aim_btn_scan();
    CHECK(aim_btn_scan() == 0x03, "L+R 同按=0x03");
    set_all(1, 1, 1);
    aim_btn_scan();
    CHECK(aim_btn_scan() == 0, "全部松开=0");

    /* 7. 极性自适应：空闲低的脚（老式 3.3V 式接线）反向也成立 */
    set_all(0, 0, 0);
    aim_btn_init();
    CHECK(g_aim_btn_open[0] == 0 && g_aim_btn_open[1] == 0 && g_aim_btn_open[2] == 0,
        "空闲低接线：开路电平 = 000");
    g_level[S_AGPIO20] = 1; /* 往 3.3V 方向闭合 */
    aim_btn_scan();
    CHECK(aim_btn_scan() == 0x01, "空闲低脚闭合到 3.3V 也判按下（0x01）");

    /* 8. 开机误按的已知行为：采样期间按住 M -> 开路电平被学成 0，松手反而报"按下" */
    set_all(1, 1, 0);
    aim_btn_init();
    CHECK(g_aim_btn_open[2] == 0, "开机按住 M：M 开路电平被学成 0");
    g_level[S_AGPIO17] = 1; /* 松手 */
    aim_btn_scan();
    CHECK(aim_btn_scan() == 0x04, "松手后反报 0x04（文档已载：开机别按，重上电恢复）");

    fprintf(stderr, "btn_sim: %d 通过, %d 失败\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
