module;

#include <cstddef>
#include <initializer_list>
#include <vector>

export module Cam.Gui.Theme;

import Rev.Element.Style;

export namespace Cam::Gui::Theme {

    using namespace Rev::Element;

    enum class Mode {
        Light,
        Dark
    };

    inline Mode mode = Mode::Light;

    struct Palette {

        Color background;

        Color panelSurface;
        Color panelBorder;
        Color panelShadow;

        Color rowSurface;
        Color rowHover;
        Color rowSelected;

        Color text;
        Color textMuted;
        Color textAccent;
        Color textOnAccent;
        Color textTab;
        Color textTabActive;

        Color buttonSurface;
        Color buttonBorder;
        Color buttonHover;
        Color buttonPress;
        Color buttonLabel;

        Color icon;
        Color iconHover;
        Color iconDisabled;

        Color accent;
        Color accentButton;
        Color accentButtonHover;

        Color tabBar;
        Color tabBarBorder;
        Color tab;
        Color tabBorder;
        Color tabHover;
        Color tabHoverBorder;
        Color tabActive;
        Color tabActiveBorder;

        Color closeHover;
        Color warning;

        bool useShadow;
    };

    inline Palette palette = {
        .background = rgba(0, 0, 0, 0.0),

        .panelSurface = rgba(255, 255, 255, 0.08),
        .panelBorder = rgba(0, 0, 0, 0.18),
        .panelShadow = rgba(0, 0, 0, 0.35),

        .rowSurface = rgba(255, 255, 255, 0.0),
        .rowHover = rgba(241, 245, 249, 1.0),
        .rowSelected = rgba(79, 99, 255, 0.08),

        .text = rgba(30, 41, 59, 1.0),
        .textMuted = rgba(100, 116, 139, 1.0),
        .textAccent = rgba(79, 99, 255, 1.0),
        .textOnAccent = rgba(255, 255, 255, 0.96),
        .textTab = rgba(0, 0, 0, 0.68),
        .textTabActive = rgba(0, 0, 0, 0.94),

        .buttonSurface = rgba(255, 255, 255, 0.24),
        .buttonBorder = rgba(0, 0, 0, 0.22),
        .buttonHover = rgba(255, 255, 255, 0.42),
        .buttonPress = rgba(255, 255, 255, 0.56),
        .buttonLabel = rgba(0, 0, 0, 0.74),

        .icon = rgba(148, 163, 184, 1.0),
        .iconHover = rgba(79, 99, 255, 1.0),
        .iconDisabled = rgba(203, 213, 225, 1.0),

        .accent = rgba(79, 99, 255, 1.0),
        .accentButton = rgba(76, 120, 220, 0.85),
        .accentButtonHover = rgba(86, 135, 245, 0.95),

        .tabBar = rgba(0, 0, 0, 0.025),
        .tabBarBorder = rgba(0, 0, 0, 0.25),
        .tab = rgba(255, 255, 255, 0.22),
        .tabBorder = rgba(0, 0, 0, 0.16),
        .tabHover = rgba(255, 255, 255, 0.40),
        .tabHoverBorder = rgba(0, 0, 0, 0.23),
        .tabActive = rgba(255, 255, 255, 0.86),
        .tabActiveBorder = rgba(0, 0, 0, 0.30),

        .closeHover = rgba(0, 0, 0, 0.12),
        .warning = rgba(245, 158, 11, 1.0),

        .useShadow = true
    };

    namespace Styles {

        Style Background = {
            .background = { .color = palette.background }
        };

        Style Panel = {
            .background = { .color = palette.panelSurface },
            .shadow = {
                .color = palette.panelShadow,
                .size = Px(-8),
                .blur = 16_px
            }
        };

        Style PanelBorder = {
            .border = {
                .color = palette.panelBorder,
                .width = 0_px
            }
        };

        Style Row = {
            .background = { .color = palette.rowSurface, .transition = 100_ms }
        };

        Style RowHover = {
            .applies = { .hover = true, .focus = true },
            .background = { .color = palette.rowHover }
        };

        Style RowSelected = {
            .background = { .color = palette.rowSelected }
        };

        Style Text = {
            .text = { .color = palette.text }
        };

        Style MutedText = {
            .text = { .color = palette.textMuted }
        };

        Style AccentText = {
            .text = { .color = palette.textAccent }
        };

        Style WarningText = {
            .text = { .color = palette.warning }
        };

        Style Button = {
            .background = { .color = palette.buttonSurface, .transition = 100_ms },
            .border = { .color = palette.buttonBorder }
        };

        Style ButtonHover = {
            .applies = { .hover = true, .focus = true },
            .background = { .color = palette.buttonHover },
            .border = { .color = palette.buttonBorder }
        };

        Style ButtonPress = {
            .applies = { .press = true },
            .background = { .color = palette.buttonPress },
            .border = { .color = palette.buttonBorder }
        };

        Style ButtonLabel = {
            .text = { .color = palette.buttonLabel }
        };

        Style Icon = {
            .text = { .color = palette.icon, .transition = 100_ms }
        };

        Style IconHover = {
            .applies = { .hover = true, .focus = true },
            .text = { .color = palette.iconHover }
        };

        Style IconDisabled = {
            .applies = { .disabled = true },
            .text = { .color = palette.iconDisabled },
            .cursor = Cursor::Default
        };

        Style DeleteIconHover = {
            .applies = { .hover = true, .focus = true },
            .text = { .color = rgba(239, 68, 68, 1.0) }
        };

        Style AccentButton = {
            .background = { .color = palette.accentButton, .transition = 100_ms }
        };

        Style AccentButtonHover = {
            .applies = { .hover = true, .focus = true },
            .background = { .color = palette.accentButtonHover }
        };

        Style AccentButtonLabel = {
            .text = { .color = palette.textOnAccent }
        };

        Style TabBar = {
            .background = { .color = palette.tabBar },
            .border = {
                .bottom = {
                    .color = palette.tabBarBorder,
                    .width = 1_px
                }
            },
            .shadow = {
                .color = rgba(0, 0, 0, 0.10),
                .size = Px(-4),
                .blur = 8_px,
                .y = 1_px
            }
        };

        Style Tab = {
            .background = { .color = palette.tab, .transition = 100_ms },
            .border = { .color = palette.tabBorder }
        };

        Style TabHover = {
            .applies = { .hover = true, .focus = true },
            .background = { .color = palette.tabHover },
            .border = { .color = palette.tabHoverBorder }
        };

        Style TabActive = {
            .background = { .color = palette.tabActive },
            .border = {
                .color = palette.tabActiveBorder,
                .bottom = {
                    .color = palette.tabActive,
                    .width = 1_px
                }
            }
        };

        Style TabLabel = {
            .text = { .color = palette.textTab }
        };

        Style TabLabelActive = {
            .text = { .color = palette.textTabActive }
        };

        Style ChromeHover = {
            .applies = { .hover = true, .focus = true },
            .background = { .color = palette.closeHover }
        };

        Style ChromeIcon = {
            .text = { .color = palette.buttonLabel, .transition = 100_ms }
        };

        Style ChromeIconHover = {
            .applies = { .hover = true, .focus = true },
            .text = { .color = palette.textTabActive }
        };
    }

    inline void markDirty(Style& style) {
        style.dirty = true;
    }

    inline Palette paletteFor(Mode value) {

        if (value == Mode::Dark) {
            return Palette {
                // VS Code / Cursor-style material dark: near-black canvas, neutral greys, outline not shadow.
                .background = rgba(30, 30, 30, 1.0),

                .panelSurface = rgba(37, 37, 38, 1.0),
                .panelBorder = rgba(69, 69, 69, 1.0),
                .panelShadow = rgba(0, 0, 0, 0.0),

                .rowSurface = rgba(0, 0, 0, 0.0),
                .rowHover = rgba(42, 45, 46, 1.0),
                .rowSelected = rgba(255, 255, 255, 0.06),

                .text = rgba(204, 204, 204, 1.0),
                .textMuted = rgba(133, 133, 133, 1.0),
                .textAccent = rgba(224, 224, 224, 1.0),
                .textOnAccent = rgba(255, 255, 255, 1.0),
                .textTab = rgba(150, 150, 150, 1.0),
                .textTabActive = rgba(224, 224, 224, 1.0),

                .buttonSurface = rgba(45, 45, 45, 1.0),
                .buttonBorder = rgba(69, 69, 69, 1.0),
                .buttonHover = rgba(55, 55, 55, 1.0),
                .buttonPress = rgba(62, 62, 62, 1.0),
                .buttonLabel = rgba(204, 204, 204, 1.0),

                .icon = rgba(150, 150, 150, 1.0),
                .iconHover = rgba(224, 224, 224, 1.0),
                .iconDisabled = rgba(90, 90, 90, 1.0),

                .accent = rgba(224, 224, 224, 1.0),
                .accentButton = rgba(45, 45, 45, 1.0),
                .accentButtonHover = rgba(55, 55, 55, 1.0),

                .tabBar = rgba(37, 37, 38, 1.0),
                .tabBarBorder = rgba(69, 69, 69, 1.0),
                .tab = rgba(45, 45, 45, 1.0),
                .tabBorder = rgba(69, 69, 69, 1.0),
                .tabHover = rgba(55, 55, 55, 1.0),
                .tabHoverBorder = rgba(85, 85, 85, 1.0),
                .tabActive = rgba(30, 30, 30, 1.0),
                .tabActiveBorder = rgba(69, 69, 69, 1.0),

                .closeHover = rgba(255, 255, 255, 0.08),
                .warning = rgba(220, 180, 80, 1.0),

                .useShadow = false
            };
        }

        return Palette {
            .background = rgba(0, 0, 0, 0.0),

            .panelSurface = rgba(255, 255, 255, 0.08),
            .panelBorder = rgba(0, 0, 0, 0.18),
            .panelShadow = rgba(0, 0, 0, 0.35),

            .rowSurface = rgba(255, 255, 255, 0.0),
            .rowHover = rgba(241, 245, 249, 1.0),
            .rowSelected = rgba(79, 99, 255, 0.08),

            .text = rgba(30, 41, 59, 1.0),
            .textMuted = rgba(100, 116, 139, 1.0),
            .textAccent = rgba(79, 99, 255, 1.0),
            .textOnAccent = rgba(255, 255, 255, 0.96),
            .textTab = rgba(0, 0, 0, 0.68),
            .textTabActive = rgba(0, 0, 0, 0.94),

            .buttonSurface = rgba(255, 255, 255, 0.24),
            .buttonBorder = rgba(0, 0, 0, 0.22),
            .buttonHover = rgba(255, 255, 255, 0.42),
            .buttonPress = rgba(255, 255, 255, 0.56),
            .buttonLabel = rgba(0, 0, 0, 0.74),

            .icon = rgba(148, 163, 184, 1.0),
            .iconHover = rgba(79, 99, 255, 1.0),
            .iconDisabled = rgba(203, 213, 225, 1.0),

            .accent = rgba(79, 99, 255, 1.0),
            .accentButton = rgba(76, 120, 220, 0.85),
            .accentButtonHover = rgba(86, 135, 245, 0.95),

            .tabBar = rgba(0, 0, 0, 0.025),
            .tabBarBorder = rgba(0, 0, 0, 0.25),
            .tab = rgba(255, 255, 255, 0.22),
            .tabBorder = rgba(0, 0, 0, 0.16),
            .tabHover = rgba(255, 255, 255, 0.40),
            .tabHoverBorder = rgba(0, 0, 0, 0.23),
            .tabActive = rgba(255, 255, 255, 0.86),
            .tabActiveBorder = rgba(0, 0, 0, 0.30),

            .closeHover = rgba(0, 0, 0, 0.12),
            .warning = rgba(245, 158, 11, 1.0),

            .useShadow = true
        };
    }

    inline void applyPalette(const Palette& value) {

        palette = value;

        using namespace Styles;

        Background.background.color = value.background;

        Panel.background.color = value.panelSurface;
        PanelBorder.border.color = value.panelBorder;

        if (value.useShadow) {
            Panel.shadow.color = value.panelShadow;
            Panel.shadow.size = Px(-8);
            Panel.shadow.blur = 16_px;
            PanelBorder.border.width = 0_px;

            TabBar.shadow.color = rgba(0, 0, 0, 0.10);
            TabBar.shadow.size = Px(-4);
            TabBar.shadow.blur = 8_px;
            TabBar.shadow.y = 1_px;
        }
        else {
            Panel.shadow.color = rgba(0, 0, 0, 0.0);
            Panel.shadow.blur = 0_px;
            PanelBorder.border.width = 1_px;

            TabBar.shadow.color = rgba(0, 0, 0, 0.0);
            TabBar.shadow.blur = 0_px;
        }

        Row.background.color = value.rowSurface;
        RowHover.background.color = value.rowHover;
        RowSelected.background.color = value.rowSelected;

        Text.text.color = value.text;
        MutedText.text.color = value.textMuted;
        AccentText.text.color = value.textAccent;
        WarningText.text.color = value.warning;

        Button.background.color = value.buttonSurface;
        Button.border.color = value.buttonBorder;
        ButtonHover.background.color = value.buttonHover;
        ButtonHover.border.color = value.buttonBorder;
        ButtonPress.background.color = value.buttonPress;
        ButtonPress.border.color = value.buttonBorder;
        ButtonLabel.text.color = value.buttonLabel;

        Icon.text.color = value.icon;
        IconHover.text.color = value.iconHover;
        IconDisabled.text.color = value.iconDisabled;

        AccentButton.background.color = value.accentButton;
        AccentButtonHover.background.color = value.accentButtonHover;
        AccentButton.border.color = value.buttonBorder;
        AccentButton.border.width = value.useShadow ? 0_px : 1_px;
        AccentButtonLabel.text.color = value.useShadow
            ? value.textOnAccent
            : value.buttonLabel;

        TabBar.background.color = value.tabBar;
        TabBar.border.bottom.color = value.tabBarBorder;

        Tab.background.color = value.tab;
        Tab.border.color = value.tabBorder;
        TabHover.background.color = value.tabHover;
        TabHover.border.color = value.tabHoverBorder;
        TabActive.background.color = value.tabActive;
        TabActive.border.color = value.tabActiveBorder;
        TabActive.border.bottom.color = value.tabActive;

        TabLabel.text.color = value.textTab;
        TabLabelActive.text.color = value.textTabActive;

        ChromeHover.background.color = value.closeHover;
        ChromeIcon.text.color = value.buttonLabel;
        ChromeIconHover.text.color = value.textTabActive;

        markDirty(Background);
        markDirty(Panel);
        markDirty(PanelBorder);
        markDirty(Row);
        markDirty(RowHover);
        markDirty(RowSelected);
        markDirty(Text);
        markDirty(MutedText);
        markDirty(AccentText);
        markDirty(WarningText);
        markDirty(Button);
        markDirty(ButtonHover);
        markDirty(ButtonPress);
        markDirty(ButtonLabel);
        markDirty(Icon);
        markDirty(IconHover);
        markDirty(IconDisabled);
        markDirty(AccentButton);
        markDirty(AccentButtonHover);
        markDirty(AccentButtonLabel);
        markDirty(TabBar);
        markDirty(Tab);
        markDirty(TabHover);
        markDirty(TabActive);
        markDirty(TabLabel);
        markDirty(TabLabelActive);
        markDirty(ChromeHover);
        markDirty(ChromeIcon);
        markDirty(ChromeIconHover);
    }

    inline void applyMode(Mode value) {
        mode = value;
        applyPalette(paletteFor(value));
    }

    inline void setMode(Mode value) {
        applyMode(value);
    }

    inline Mode currentMode() {
        return mode;
    }

    inline void toggleMode() {
        applyMode(mode == Mode::Light ? Mode::Dark : Mode::Light);
    }

    inline size_t firstConditionalStyle(std::initializer_list<Style*> component) {

        size_t index = 0;

        for (Style* style : component) {

            if (
                style->applies.hover ||
                style->applies.press ||
                style->applies.focus ||
                style->applies.disabled
            ) {
                return index;
            }

            index += 1;
        }

        return index;
    }

    inline StyleList layer(
        std::initializer_list<Style*> component,
        std::initializer_list<Style*> theme
    ) {
        std::vector<Style*> styles(component.begin(), component.end());

        size_t at = firstConditionalStyle(component);

        styles.insert(
            styles.begin() + static_cast<std::ptrdiff_t>(at),
            theme.begin(),
            theme.end()
        );

        return StyleList(std::move(styles));
    }

    inline StyleList withPanel(std::initializer_list<Style*> component) {
        return layer(component, { &Styles::Panel, &Styles::PanelBorder });
    }

    inline StyleList withButton(std::initializer_list<Style*> component) {
        return layer(component, { &Styles::Button });
    }

    inline StyleList withTab(std::initializer_list<Style*> component) {
        return layer(component, { &Styles::Tab });
    }

    inline StyleList withText(std::initializer_list<Style*> component) {
        std::vector<Style*> styles(component.begin(), component.end());
        styles.push_back(&Styles::Text);
        return StyleList(std::move(styles));
    }

    inline StyleList withMutedText(std::initializer_list<Style*> component) {
        std::vector<Style*> styles(component.begin(), component.end());
        styles.push_back(&Styles::MutedText);
        return StyleList(std::move(styles));
    }

    inline StyleList withIcon(std::initializer_list<Style*> component) {
        return layer(component, { &Styles::Icon });
    }
}
