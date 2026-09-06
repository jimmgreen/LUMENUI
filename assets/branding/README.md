# LUMEN 玻璃日蚀图标

主体沿用项目现有日蚀光环，参考图片仅用于玻璃材质。黑、灰、白单色；无文字。

## 推荐使用

- `lumen-eclipse-glass.png`：imagegen 最终原图，1254 × 1254，黑色背景，RGB，非透明。
- `lumen-eclipse-glass-1024.png`：1024 × 1024 导出版。另含 512、256、128、64、48、32、16 尺寸。
- `lumen-eclipse-glass.ico`：Windows 图标，包含 16、32、48、64、128、256 六档。

最终版增强了玻璃厚度、透射暗部与曲面折射，减少了过曝泛光和底部焦散。小尺寸保留环形轮廓，玻璃细节以 128px 及以上更清楚。

## 可编辑矢量备选

`lumen-eclipse.svg` 为独立手绘纯矢量版本，外部透明；`lumen-eclipse-vector-*.png` 为其导出图。它是风格简化备选，不是最终生成图片的无损 SVG 复刻。真实玻璃折射以推荐 PNG 为准。

`lumen-eclipse-imagegen.png` 为早期日蚀方案，仅供对照。最终素材使用 `lumen-eclipse-glass` 前缀。

生成方式：内置 imagegen，以项目 `icon.png` 为标识参考，用户截图为材质参考，再进行了两轮材质与轮廓优化。提示词见 `imagegen-prompt.txt`。未替换根目录 `icon.png`，未修改应用代码。
