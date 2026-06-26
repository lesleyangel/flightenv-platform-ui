#pragma once

// FlightEnv Twin Workbench 设计调色板。
// 这些常量与 htmlUI/styles.css 的设计 token 一一对应，供需要自绘的组件
// （状态药丸、stage 顶部色条、趋势曲线）直接使用；纯样式部分由 theme/twin.qss 负责。
#include <QColor>

namespace twin::palette {

// surfaces
inline QColor bg()        { return QColor(0xee, 0xf0, 0xf3); }
inline QColor bgSunken()  { return QColor(0xe7, 0xea, 0xee); }
inline QColor panel()     { return QColor(0xff, 0xff, 0xff); }
inline QColor panel2()    { return QColor(0xf7, 0xf8, 0xfa); }
inline QColor panel3()    { return QColor(0xf1, 0xf3, 0xf6); }
inline QColor rail()      { return QColor(0xfb, 0xfb, 0xfc); }

// borders
inline QColor line()      { return QColor(0xe2, 0xe6, 0xec); }
inline QColor line2()     { return QColor(0xd6, 0xdb, 0xe2); }
inline QColor line3()     { return QColor(0xc6, 0xcc, 0xd6); }

// text
inline QColor ink()       { return QColor(0x1b, 0x1f, 0x27); }
inline QColor ink2()      { return QColor(0x4b, 0x53, 0x60); }
inline QColor ink3()      { return QColor(0x79, 0x81, 0x8f); }
inline QColor ink4()      { return QColor(0xa4, 0xab, 0xb6); }

// accent — engineering cyan
inline QColor acc()       { return QColor(0x0e, 0x8a, 0x9c); }
inline QColor accStrong() { return QColor(0x0a, 0x6c, 0x7b); }
inline QColor accInk()    { return QColor(0x07, 0x58, 0x66); }
inline QColor accSoft()   { return QColor(0xe0, 0xf2, 0xf4); }
inline QColor accSoft2()  { return QColor(0xcd, 0xe9, 0xec); }

// status
inline QColor ok()        { return QColor(0x1f, 0x9d, 0x57); }
inline QColor okSoft()    { return QColor(0xe4, 0xf4, 0xea); }
inline QColor warn()      { return QColor(0xb0, 0x7d, 0x09); }
inline QColor warnSoft()  { return QColor(0xf8, 0xef, 0xd5); }
inline QColor fail()      { return QColor(0xd2, 0x3f, 0x3f); }
inline QColor failSoft()  { return QColor(0xfa, 0xe6, 0xe6); }
inline QColor unk()       { return QColor(0x8a, 0x90, 0x9c); }
inline QColor unkSoft()   { return QColor(0xed, 0xef, 0xf2); }

// 四个物理场配色（与 htmlUI field selector 一致：P 压力 / K 热流 / S 应力 / T 温度）
inline QColor fieldP()    { return QColor(0x5b, 0x6b, 0xff); }
inline QColor fieldK()    { return QColor(0xe2, 0x6a, 0x2c); }
inline QColor fieldS()    { return QColor(0x9b, 0x59, 0xb6); }
inline QColor fieldT()    { return QColor(0xd2, 0x3f, 0x3f); }

// 圆角
constexpr int radiusS = 4;
constexpr int radiusM = 6;
constexpr int radiusL = 9;

} // namespace twin::palette
