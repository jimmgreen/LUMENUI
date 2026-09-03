// lumen/Animate.h — 时程动画原语（CSS/WAAPI 缓动、补间、弹簧）。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
// 绘制路径不得分配；本头的计算全是栈上标量。光斑位置禁止走补间（会慢一拍）。
#pragma once
#include "Core.h"
#include <cstdint>

namespace lumen {

// CSS2 命名曲线、Material standard、GSAP/anime 常用多项式、自定义三次贝塞尔。
enum class Ease : uint8_t {
    Linear,
    CssEase,        // cubic-bezier(0.25, 0.1, 0.25, 1)
    CssEaseIn,      // cubic-bezier(0.42, 0, 1, 1)
    CssEaseOut,     // cubic-bezier(0, 0, 0.58, 1)
    CssEaseInOut,   // cubic-bezier(0.42, 0, 0.58, 1)
    Material,       // cubic-bezier(0.4, 0, 0.2, 1)  Tailwind/M3 standard
    InQuad,
    OutQuad,
    InOutQuad,
    InCubic,
    OutCubic,
    InOutCubic,
    InSine,
    OutSine,
    InOutSine,
    InExpo,
    OutExpo,
    InOutExpo,
    OutBack,        // 轻微过冲后回落
    Bezier,         // 使用传入的 CubicBezier
};

struct CubicBezier {
    float x1 = 0.0f, y1 = 0.0f, x2 = 1.0f, y2 = 1.0f;

    static constexpr CubicBezier Linear() noexcept { return {0.0f, 0.0f, 1.0f, 1.0f}; }
    static constexpr CubicBezier CssEase() noexcept { return {0.25f, 0.1f, 0.25f, 1.0f}; }
    static constexpr CubicBezier CssEaseIn() noexcept { return {0.42f, 0.0f, 1.0f, 1.0f}; }
    static constexpr CubicBezier CssEaseOut() noexcept { return {0.0f, 0.0f, 0.58f, 1.0f}; }
    static constexpr CubicBezier CssEaseInOut() noexcept { return {0.42f, 0.0f, 0.58f, 1.0f}; }
    static constexpr CubicBezier Material() noexcept { return {0.4f, 0.0f, 0.2f, 1.0f}; }
};

// t 夹到 [0,1]。OutBack 中段可超出 [0,1]；其余常用曲线单调。
float EaseAt(float t, Ease ease) noexcept;
float EaseAt(float t, CubicBezier bezier) noexcept;
CubicBezier BezierFor(Ease ease) noexcept;

// 一阶指数趋近（控件悬停辉光等）。到位返回 false。
bool EaseTo(float& value, float target, float dt, float speed = 12.0f,
            float epsilon = 0.002f) noexcept;

// WAAPI/CSS transition：duration 秒内 from→to。delay 在时长之前。
struct Tween {
    float from = 0.0f;
    float to = 1.0f;
    float duration = 0.3f;
    float delay = 0.0f;
    Ease ease = Ease::CssEaseInOut;
    CubicBezier bezier = CubicBezier::CssEaseInOut();
    float elapsed = 0.0f;
    bool running = false;

    void Play(float from_value, float to_value, float duration_seconds,
              Ease ease = Ease::CssEaseInOut) noexcept;
    void PlayBezier(float from_value, float to_value, float duration_seconds,
                    CubicBezier curve) noexcept;
    void Snap(float value) noexcept;
    bool Tick(float dt_seconds) noexcept;   // 仍在播返回 true
    float T() const noexcept;               // 过 delay 后的 0..1 时间进度
    float Value() const noexcept;           // 缓动后的标量
};

// 阻尼谐振子（Framer/react-spring：k、c、m）。分析解，16ms 抖动也不易炸。
struct Spring {
    float stiffness = 170.0f;
    float damping = 26.0f;
    float mass = 1.0f;
    float rest_delta = 0.001f;
    float rest_speed = 0.01f;

    static Spring Smooth() noexcept { return {170.0f, 26.0f, 1.0f}; }   // 近临界
    static Spring Snappy() noexcept { return {450.0f, 35.0f, 1.0f}; }   // 略过冲
    static Spring Gentle() noexcept { return {80.0f, 18.0f, 1.0f}; }
    static Spring Critical(float stiffness = 170.0f) noexcept;
};

struct SpringMotion {
    float value = 0.0f;
    float velocity = 0.0f;

    void Snap(float v) noexcept;
    bool Tick(float target, float dt_seconds, const Spring& spring = Spring::Smooth()) noexcept;
};

// 按住连发：按下由调用方先 Fire 一次，之后 delay 秒起、每 interval 秒再 Tick 返回次数。
// 绘制路径零堆。掉帧时单帧最多 8 次，避免一次 hitch 把计数器打爆。
struct RepeatHold {
    float delay = 0.40f;
    float interval = 0.05f;
    bool armed = false;
    bool repeating = false;
    float wait = 0.0f;

    void Press() noexcept {
        armed = true;
        repeating = false;
        wait = delay > 0.0f ? delay : 0.0f;
    }
    void Release() noexcept {
        armed = false;
        repeating = false;
        wait = 0.0f;
    }
    int Tick(float dt, bool active) noexcept {
        if (!armed || !active || dt <= 0.0f) return 0;
        wait -= dt;
        int n = 0;
        const float step = interval > 1.0e-4f ? interval : 1.0e-4f;
        while (wait <= 0.0f && n < 8) {
            ++n;
            repeating = true;
            wait += step;
        }
        return n;
    }
};

} // namespace lumen
