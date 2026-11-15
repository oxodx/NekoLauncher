#pragma once
#include <QtGui>
namespace neko {
struct Appearance {
    QString base, mantle, surface, overlay, text, subtext, accent, accentText;
    bool light = false;
};
inline Appearance appearance(const QString &name) {
    if (name == "Shadcn Dark")
        return {"#09090b", "#0c0c0f", "#18181b", "#3f3f46", "#fafafa", "#a1a1aa", "#fafafa", "#18181b"};
    if (name == "Shadcn Light")
        return {"#ffffff", "#fafafa", "#f4f4f5", "#d4d4d8", "#09090b", "#71717a", "#18181b", "#fafafa", true};
    if (name == "Catppuccin Latte")
        return {"#eff1f5", "#e6e9ef", "#ccd0da", "#bcc0cc", "#4c4f69", "#5c5f77", "#8839ef", "#eff1f5", true};
    if (name == "Catppuccin Frappé")
        return {"#303446", "#292c3c", "#414559", "#51576d", "#c6d0f5", "#a5adce", "#ca9ee6", "#232634"};
    if (name == "Catppuccin Macchiato")
        return {"#24273a", "#1e2030", "#363a4f", "#494d64", "#cad3f5", "#a5adcb", "#c6a0f6", "#181926"};
    if (name == "Catppuccin Mocha")
        return {"#1e1e2e", "#181825", "#313244", "#45475a", "#cdd6f4", "#a6adc8", "#cba6f7", "#11111b"};
    if (name == "Light")
        return {"#f4f5fa", "#ffffff", "#e7eaf2", "#dce0eb", "#1b293c", "#5c6c80", "#6750b9", "#ffffff", true};
    return {"#10121c",
            "#171a28",
            "#24283b",
            "#343b53",
            "#e9edf5",
            "#99a5be",
            name == "Forest" ? "#83d6ad"
            : name == "Rose" ? "#efa7c1"
                             : "#b8adff",
            "#17162a"};
}
} // namespace neko
