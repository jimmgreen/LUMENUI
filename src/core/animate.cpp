#include "lumen/Animate.h"
#include <cmath>

namespace lumen {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kBezierEps = 1.0e-6f;

float Clamp01(float t) noexcept { return Clamp(t, 0.0f, 1.0f); }

// WebKit UnitBezier：先对 x 牛顿/二分求 t，再取样 y。x1/x2 按 CSS 夹到 [0,1]。
struct UnitBezier {
    float ax, bx, cx, ay, by, cy;

    explicit UnitBezier(CubicBezier c) noexcept {
        const float x1 = Clamp01(c.x1);
        const float x2 = Clamp01(c.x2);
        cx = 3.0f * x1;
        bx = 3.0f * (x2 - x1) - cx;
        ax = 1.0f - cx - bx;
        cy = 3.0f * c.y1;
        by = 3.0f * (c.y2 - c.y1) - cy;
        ay = 1.0f - cy - by;
    }

    float SampleX(float t) const noexcept { return ((ax * t + bx) * t + cx) * t; }
    float SampleY(float t) const noexcept { return ((ay * t + by) * t + cy) * t; }
    float SampleDx(float t) const noexcept { return (3.0f * ax * t + 2.0f * bx) * t + cx; }

    float SolveX(float x) const noexcept {
        float t = x;
        for (int i = 0; i < 8; ++i) {
            const float err = SampleX(t) - x;
            if (std::fabs(err) < kBezierEps) return t;
            const float d = SampleDx(t);
            if (std::fabs(d) < kBezierEps) break;
            t -= err / d;
        }
        float lo = 0.0f, hi = 1.0f;
        t = Clamp01(t);
        while (hi - lo > kBezierEps) {
            const float x2 = SampleX(t);
            if (std::fabs(x2 - x) < kBezierEps) return t;
            if (x > x2) lo = t;
            else hi = t;
            t = (lo + hi) * 0.5f;
        }
        return t;
    }

    float Solve(float x) const noexcept { return SampleY(SolveX(Clamp01(x))); }
};

float PolyEase(float t, Ease ease) noexcept {
    t = Clamp01(t);
    switch (ease) {
    case Ease::Linear:
        return t;
    case Ease::InQuad:
        return t * t;
    case Ease::OutQuad: {
        const float u = 1.0f - t;
        return 1.0f - u * u;
    }
    case Ease::InOutQuad:
        return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
    case Ease::InCubic:
        return t * t * t;
    case Ease::OutCubic: {
        const float u = 1.0f - t;
        return 1.0f - u * u * u;
    }
    case Ease::InOutCubic:
        return t < 0.5f ? 4.0f * t * t * t
                        : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    case Ease::InSine:
        return 1.0f - std::cos(t * kPi * 0.5f);
    case Ease::OutSine:
        return std::sin(t * kPi * 0.5f);
    case Ease::InOutSine:
        return -(std::cos(kPi * t) - 1.0f) * 0.5f;
    case Ease::InExpo:
        return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
    case Ease::OutExpo:
        return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
    case Ease::InOutExpo:
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;
        return t < 0.5f ? std::pow(2.0f, 20.0f * t - 10.0f) * 0.5f
                        : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) * 0.5f;
    case Ease::OutBack: {
        constexpr float c1 = 1.70158f;
        constexpr float c3 = c1 + 1.0f;
        const float u = t - 1.0f;
        return 1.0f + c3 * u * u * u + c1 * u * u;
    }
    default:
        return t;
    }
}

bool UsesBezier(Ease ease) noexcept {
    switch (ease) {
    case Ease::CssEase:
    case Ease::CssEaseIn:
    case Ease::CssEaseOut:
    case Ease::CssEaseInOut:
    case Ease::Material:
    case Ease::Bezier:
        return true;
    default:
        return false;
    }
}

} // namespace

CubicBezier BezierFor(Ease ease) noexcept {
    switch (ease) {
    case Ease::CssEase:
        return CubicBezier::CssEase();
    case Ease::CssEaseIn:
        return CubicBezier::CssEaseIn();
    case Ease::CssEaseOut:
        return CubicBezier::CssEaseOut();
    case Ease::CssEaseInOut:
        return CubicBezier::CssEaseInOut();
    case Ease::Material:
        return CubicBezier::Material();
    default:
        return CubicBezier::Linear();
    }
}

float EaseAt(float t, CubicBezier bezier) noexcept {
    t = Clamp01(t);
    if (t == 0.0f || t == 1.0f) return t;
    return UnitBezier(bezier).Solve(t);
}

float EaseAt(float t, Ease ease) noexcept {
    t = Clamp01(t);
    if (UsesBezier(ease)) {
        if (ease == Ease::Bezier) return EaseAt(t, CubicBezier::Linear());
        return EaseAt(t, BezierFor(ease));
    }
    return PolyEase(t, ease);
}

bool EaseTo(float& value, float target, float dt, float speed, float epsilon) noexcept {
    const float diff = target - value;
    if (std::fabs(diff) <= epsilon) {
        value = target;
        return false;
    }
    value += diff * (1.0f - std::exp(-speed * dt));
    return true;
}

void Tween::Play(float from_value, float to_value, float duration_seconds, Ease e) noexcept {
    from = from_value;
    to = to_value;
    duration = duration_seconds;
    ease = e;
    elapsed = 0.0f;
    if (duration <= 0.0f) {
        Snap(to_value);
        return;
    }
    running = true;
}

void Tween::PlayBezier(float from_value, float to_value, float duration_seconds,
                       CubicBezier curve) noexcept {
    bezier = curve;
    Play(from_value, to_value, duration_seconds, Ease::Bezier);
}

void Tween::Snap(float value) noexcept {
    from = to = value;
    elapsed = delay + (duration > 0.0f ? duration : 0.0f);
    running = false;
}

float Tween::T() const noexcept {
    if (duration <= 0.0f) return 1.0f;
    return Clamp01((elapsed - delay) / duration);
}

float Tween::Value() const noexcept {
    const float e = (ease == Ease::Bezier) ? EaseAt(T(), bezier) : EaseAt(T(), ease);
    return Lerp(from, to, e);
}

bool Tween::Tick(float dt_seconds) noexcept {
    if (!running) return false;
    elapsed += dt_seconds;
    if (elapsed - delay >= duration) {
        elapsed = delay + duration;
        running = false;
        return false;
    }
    return true;
}

Spring Spring::Critical(float stiffness) noexcept {
    const float k = stiffness > 1.0e-4f ? stiffness : 1.0e-4f;
    return {k, 2.0f * std::sqrt(k), 1.0f};
}

void SpringMotion::Snap(float v) noexcept {
    value = v;
    velocity = 0.0f;
}

bool SpringMotion::Tick(float target, float dt, const Spring& spring) noexcept {
    if (dt <= 0.0f) {
        return std::fabs(value - target) > spring.rest_delta ||
               std::fabs(velocity) > spring.rest_speed;
    }
    const float mass = spring.mass > 1.0e-4f ? spring.mass : 1.0e-4f;
    const float k = spring.stiffness > 1.0e-4f ? spring.stiffness : 1.0e-4f;
    const float omega0 = std::sqrt(k / mass);
    const float zeta = spring.damping / (2.0f * std::sqrt(k * mass));
    const float x0 = value - target;
    const float v0 = velocity;

    float x = x0;
    float v = v0;
    if (zeta < 1.0f - 1.0e-3f) {
        const float wd = omega0 * std::sqrt(1.0f - zeta * zeta);
        const float a = x0;
        const float b = (v0 + zeta * omega0 * x0) / wd;
        const float decay = std::exp(-zeta * omega0 * dt);
        const float c = std::cos(wd * dt);
        const float s = std::sin(wd * dt);
        x = decay * (a * c + b * s);
        v = decay * ((-zeta * omega0) * (a * c + b * s) + wd * (-a * s + b * c));
    } else if (zeta > 1.0f + 1.0e-3f) {
        const float wr = omega0 * std::sqrt(zeta * zeta - 1.0f);
        const float r1 = -zeta * omega0 + wr;
        const float r2 = -zeta * omega0 - wr;
        const float den = r1 - r2;
        const float c1 = (v0 - r2 * x0) / den;
        const float c2 = x0 - c1;
        const float e1 = std::exp(r1 * dt);
        const float e2 = std::exp(r2 * dt);
        x = c1 * e1 + c2 * e2;
        v = c1 * r1 * e1 + c2 * r2 * e2;
    } else {
        const float decay = std::exp(-omega0 * dt);
        const float b = v0 + omega0 * x0;
        x = decay * (x0 + b * dt);
        v = decay * (v0 - omega0 * b * dt);
    }

    value = x + target;
    velocity = v;
    if (std::fabs(value - target) <= spring.rest_delta &&
        std::fabs(velocity) <= spring.rest_speed) {
        value = target;
        velocity = 0.0f;
        return false;
    }
    return true;
}

} // namespace lumen
