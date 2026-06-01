module;

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <functional>

export module Cam.Gui.StockMenu;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Dropdown;
import Rev.Element.NumberInput;
import Rev.Element.Button;

import Cam.App;
import Cam.App.Project;

import Cam.Gui.Theme;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace StockMenuStyle::Styles {

        Style Self = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 100_pct },
            .margin = { .top = 8_px, .bottom = 2_px },
            .padding = { .left = 2_px, .right = 2_px, .top = 8_px, .bottom = 4_px },
            .border = { .top = { .width = 1_px } }
        };

        Style Title = {
            .size = { .width = 100_pct },
            .margin = { .bottom = 6_px },
            .text = { .size = 12_px }
        };

        Style Row = {
            .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 100_pct },
            .margin = { .bottom = 4_px }
        };

        Style Field = {
            .size = { .width = Grow() },
            .margin = { .left = 2_px, .right = 2_px }
        };

        Style TypeField = {
            .size = { .width = 100_pct },
            .margin = { .bottom = 6_px }
        };

        Style LengthLabel = {
            .margin = { .top = 2_px, .left = 2_px },
            .text = { .size = 11_px }
        };

        Style GenerateButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .width = 100_pct },
            .margin = { .top = 2_px, .bottom = 2_px }
        };
    };

    using namespace StockMenuStyle;

    struct StockMenu : public Box {

        Cam::App::AppState* app = nullptr;

        Text* title = nullptr;
        Button* generateButton = nullptr;
        Dropdown* typeDropdown = nullptr;

        Box* prismRow = nullptr;
        NumberInput* widthInput = nullptr;
        NumberInput* heightInput = nullptr;

        Box* cylinderRow = nullptr;
        NumberInput* radiusInput = nullptr;
        NumberInput* diameterInput = nullptr;

        Text* lengthLabel = nullptr;

        bool populated = false;

        // Minimum dimensions that still contain the part.  Enforced as a clamp
        // ON COMMIT rather than a NumberInput typing bound, because a typing
        // bound rejects intermediate keystrokes (e.g. "1" while typing "12"
        // when the minimum is 7) and makes the field feel uneditable.
        double minWidth = 0.0;
        double minHeight = 0.0;
        double minRadius = 0.0;

        std::function<void(Event&)> onChanged;

        StockMenu(Element* parent, StyleList styles = {}) : Box(parent, styles, "StockMenu") {

            app = Cam::App::AppState::Get(shared->state);

            this->styles.add(&Styles::Self);
            this->styles.add(&Theme::Styles::PanelBorder);

            title = new Text(
                this,
                "Stock",
                Theme::withMutedText({ &Styles::Title })
            );

            generateButton = new Button(
                this,
                Button::Params::Primary("Generate Stock"),
                { &Styles::GenerateButton }
            );

            generateButton->onClick([this](Event& e) {

                Cam::App::Project* project = activeProject();

                if (project && project->generateStock()) {
                    populated = false;            // repopulate fields from new defaults
                    if (onChanged) { onChanged(e); }
                }

                refresh(e);
                e.propagate = false;
            });

            typeDropdown = new Dropdown(
                this,
                {
                    .label = "Stock type",
                    .options = {
                        { "Rectangular Prism", "prism" },
                        { "Cylinder", "cylinder" }
                    },
                    .placeholder = "Select type",
                    .value = "prism"
                },
                { &Styles::TypeField }
            );

            typeDropdown->onChange = [this](Event& e) {

                Cam::App::Project* project = activeProject();

                if (project) {
                    project->stock.type = (typeDropdown->params.value == "cylinder")
                        ? Cam::App::StockType::Cylinder
                        : Cam::App::StockType::RectangularPrism;
                }

                updateFieldVisibility();
                applyStockChange(e);
            };

            // Rectangular prism fields
            prismRow = new Box(this, { &Styles::Row }, "StockPrismRow");

                widthInput = new NumberInput(
                    prismRow,
                    dimensionParams("Width (mm)", "10"),
                    { &Styles::Field }
                );

                heightInput = new NumberInput(
                    prismRow,
                    dimensionParams("Height (mm)", "10"),
                    { &Styles::Field }
                );

                widthInput->onValueChange = [this](Event& e, std::optional<double> v) {
                    if (!v) { return; }
                    const double val = std::max(*v, minWidth);
                    if (Cam::App::Project* p = activeProject()) { p->stock.width = val; }
                    if (std::fabs(val - *v) > 1e-9) { widthInput->setValue(val); }
                    applyStockChange(e);
                };

                heightInput->onValueChange = [this](Event& e, std::optional<double> v) {
                    if (!v) { return; }
                    const double val = std::max(*v, minHeight);
                    if (Cam::App::Project* p = activeProject()) { p->stock.height = val; }
                    if (std::fabs(val - *v) > 1e-9) { heightInput->setValue(val); }
                    applyStockChange(e);
                };

            // Cylinder fields (radius <-> diameter linked)
            cylinderRow = new Box(this, { &Styles::Row }, "StockCylinderRow");

                radiusInput = new NumberInput(
                    cylinderRow,
                    dimensionParams("Radius (mm)", "5"),
                    { &Styles::Field }
                );

                diameterInput = new NumberInput(
                    cylinderRow,
                    dimensionParams("Diameter (mm)", "10"),
                    { &Styles::Field }
                );

                radiusInput->onValueChange = [this](Event& e, std::optional<double> v) {
                    if (!v) { return; }
                    applyRadius(*v, e);
                };

                diameterInput->onValueChange = [this](Event& e, std::optional<double> v) {
                    if (!v) { return; }
                    applyRadius(*v * 0.5, e);
                };

            lengthLabel = new Text(
                this,
                "Length (axis): -",
                Theme::withMutedText({ &Styles::LengthLabel })
            );
        }

        static NumberInput::Params dimensionParams(
            const char* label,
            const char* placeholder
        ) {
            NumberInput::Params p;
            p.label = label;
            p.placeholder = placeholder;
            p.maxLength = 32;
            p.selectAllOnFocus = true;
            p.allowNegative = false;
            p.allowDecimal = true;
            p.allowEmpty = false;
            p.maxDecimalPlaces = 3;
            return p;
        }

        Cam::App::Project* activeProject() {

            if (!app) { return nullptr; }

            return app->activeProject;
        }

        void applyStockChange(Event& e) {

            Cam::App::Project* project = activeProject();

            if (!project) { return; }

            project->regenerateStockStates();

            if (onChanged) { onChanged(e); }

            refresh(e);
        }

        // Clamp a proposed radius to the part-containing minimum and update both
        // linked fields plus the stock definition, then regenerate.
        void applyRadius(double radius, Event& e) {

            const double val = std::max(radius, minRadius);

            if (Cam::App::Project* p = activeProject()) { p->stock.radius = val; }

            // setValue without an event does not re-fire onValueChange.
            radiusInput->setValue(val);
            diameterInput->setValue(val * 2.0);

            applyStockChange(e);
        }

        void updateFieldVisibility() {

            Cam::App::Project* project = activeProject();

            const bool cylinder =
                project &&
                project->stock.type == Cam::App::StockType::Cylinder;

            if (prismRow) {
                prismRow->style->visibility = cylinder ? Visibility::Hidden : Visibility::Visible;
            }

            if (cylinderRow) {
                cylinderRow->style->visibility = cylinder ? Visibility::Visible : Visibility::Hidden;
            }
        }

        // Cache the part-containing minima (used only as commit-time clamps).
        void updateMinima(Cam::App::Project* project) {

            double extX = 0.0, extY = 0.0, extZ = 0.0;

            if (!project->partFrameExtents(extX, extY, extZ)) { return; }

            minWidth  = extY;
            minHeight = extZ;
            minRadius = 0.5 * std::sqrt(extY * extY + extZ * extZ);
        }

        void populateFromStock(Cam::App::Project* project) {

            const Cam::App::StockDefinition& stock = project->stock;

            const bool cylinder = stock.type == Cam::App::StockType::Cylinder;

            typeDropdown->params.value = cylinder ? "cylinder" : "prism";
            typeDropdown->dropdownText->content = cylinder ? "Cylinder" : "Rectangular Prism";

            if (widthInput)    { widthInput->setValue(stock.width); }
            if (heightInput)   { heightInput->setValue(stock.height); }
            if (radiusInput)   { radiusInput->setValue(stock.radius); }
            if (diameterInput) { diameterInput->setValue(stock.radius * 2.0); }
        }

        void updateLengthLabel(Cam::App::Project* project) {

            if (!lengthLabel) { return; }

            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "Length (axis): %.2f mm", project->stock.length);
            lengthLabel->content = buffer;
        }

        void setControlsVisible(bool fields) {

            // Before generation: only the "Generate Stock" button shows.
            // After generation: the dimension controls show.
            const Visibility fieldVis  = fields ? Visibility::Visible : Visibility::Hidden;
            const Visibility buttonVis = fields ? Visibility::Hidden  : Visibility::Visible;

            if (generateButton) { generateButton->style->visibility = buttonVis; }
            if (typeDropdown)   { typeDropdown->style->visibility = fieldVis; }
            if (lengthLabel)    { lengthLabel->style->visibility = fieldVis; }

            if (!fields) {
                if (prismRow)    { prismRow->style->visibility = Visibility::Hidden; }
                if (cylinderRow) { cylinderRow->style->visibility = Visibility::Hidden; }
            }
        }

        void computeChildren(Event& e) override {

            Cam::App::Project* project = activeProject();

            const bool available = project && project->stockMenuAvailable();

            this->style->visibility = available ? Visibility::Visible : Visibility::Hidden;

            if (!available) {
                populated = false;
                Box::computeChildren(e);
                return;
            }

            const bool defined = project->stock.defined;

            setControlsVisible(defined);

            if (!defined) {
                populated = false;
                Box::computeChildren(e);
                return;
            }

            updateMinima(project);

            if (!populated) {
                populateFromStock(project);
                populated = true;
            }

            updateFieldVisibility();
            updateLengthLabel(project);

            Box::computeChildren(e);
        }
    };
}
