module;

#include <cmath>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <algorithm>
#include <cctype>
#include <cstdio>

export module Rev.Element.NumberInput;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.TextInput;
import Rev.Element.Text;

export namespace Rev::Element {

    struct NumberInput : public TextInput {

        struct Params : TextInput::Params {

            bool allowNegative = true;
            bool allowDecimal = true;
            bool allowEmpty = true;

            int maxDecimalPlaces = 8;

            std::optional<double> min;
            std::optional<double> max;
            std::vector<double> forbiddenValues;

            static Params Default() {
                Params p;
                p.label = "Number";
                p.placeholder = "0";
                p.maxLength = 32;
                p.selectAllOnFocus = true;
                p.allowNegative = true;
                p.allowDecimal = true;
                p.allowEmpty = true;
                p.maxDecimalPlaces = 8;
                return p;
            }
        };

        Params numberParams;

        std::string lastCommittedText;
        std::optional<double> committedValue;

        std::function<void(Event&, std::optional<double>)> onValueChange;

        NumberInput(
            Element* parent,
            Params p = Params::Default(),
            StyleList styles = {}
        ) : TextInput(parent, p, styles) {

            name = "NumberInput";
            numberParams = p;

            lastCommittedText = text->content.get();

            if (!lastCommittedText.empty()) {
                if (std::optional<double> parsed = ingestFromText(lastCommittedText)) {
                    committedValue = interpretValue(*parsed, numberParams);
                }
            }

            text->onLoseFocus([this](Event& e) {
                commitOnLoseFocus(e);
            });
        }

        static bool isCompleteNumber(const std::string& text) {

            if (text.empty()) {
                return false;
            }

            char last = text.back();

            return std::isdigit((unsigned char)last) != 0;
        }

        static bool isForbiddenValue(
            double value,
            const std::vector<double>& forbidden
        ) {

            for (double entry : forbidden) {

                if (std::fabs(value - entry) < 1e-9) {
                    return true;
                }
            }

            return false;
        }

        static bool satisfiesTypingBounds(
            double value,
            const Params& p
        ) {

            if (!p.allowNegative && value < 0.0) {
                return false;
            }

            if (p.min && value < *p.min) {
                return false;
            }

            if (p.max && value > *p.max) {
                return false;
            }

            if (isForbiddenValue(value, p.forbiddenValues)) {
                return false;
            }

            return true;
        }

        static std::optional<double> tryParse(const std::string& text) {

            if (
                text.empty() ||
                text == "-" ||
                text == "." ||
                text == "-."
            ) {
                return std::nullopt;
            }

            try {
                size_t consumed = 0;
                double value = std::stod(text, &consumed);

                if (consumed != text.size()) {
                    return std::nullopt;
                }

                return value;
            }

            catch (...) {
                return std::nullopt;
            }
        }

        static std::optional<double> ingestFromText(const std::string& raw) {

            if (std::optional<double> parsed = tryParse(raw)) {
                return parsed;
            }

            std::string trimmed = raw;

            while (
                !trimmed.empty() &&
                (trimmed.back() == '.' || trimmed.back() == '-')
            ) {
                trimmed.pop_back();
            }

            if (trimmed.empty() || trimmed == "-") {
                return std::nullopt;
            }

            return tryParse(trimmed);
        }

        static double roundToDecimalPlaces(
            double value,
            int decimalPlaces
        ) {

            if (decimalPlaces < 0) {
                return value;
            }

            double factor = std::pow(10.0, decimalPlaces);
            return std::round(value * factor) / factor;
        }

        static double interpretValue(
            double value,
            const Params& p
        ) {

            if (!p.allowNegative && value < 0.0) {
                value = 0.0;
            }

            if (p.min) {
                value = std::max(value, *p.min);
            }

            if (p.max) {
                value = std::min(value, *p.max);
            }

            if (isForbiddenValue(value, p.forbiddenValues)) {

                if (p.min) {
                    value = *p.min;
                }

                else if (p.max) {
                    value = *p.max;
                }

                else if (!p.allowNegative) {
                    value = 0.0;
                }
            }

            if (p.allowDecimal && p.maxDecimalPlaces >= 0) {
                value = roundToDecimalPlaces(value, p.maxDecimalPlaces);
            }

            else {
                value = std::round(value);
            }

            return value;
        }

        static std::string formatForDisplay(
            double value,
            const Params& p
        ) {

            if (!p.allowDecimal || p.maxDecimalPlaces <= 0) {
                char buffer[64];
                std::snprintf(buffer, sizeof(buffer), "%.0f", value);
                return buffer;
            }

            char buffer[64];
            std::snprintf(
                buffer,
                sizeof(buffer),
                "%.*f",
                p.maxDecimalPlaces,
                value
            );

            return buffer;
        }

        void applyDisplay(
            const std::string& display,
            std::optional<double> value,
            Event& e
        ) {

            text->content = display;
            lastCommittedText = display;
            committedValue = value;
            refresh(e);
        }

        void restoreLastCommitted(Event& e) {

            applyDisplay(lastCommittedText, committedValue, e);
        }

        bool isValidPartial(const std::string& text) const {

            if (text.empty()) {
                return numberParams.allowEmpty;
            }

            if (!numberParams.allowNegative && text[0] == '-') {
                return false;
            }

            if (!numberParams.allowDecimal && text.find('.') != std::string::npos) {
                return false;
            }

            bool seenDot = false;
            size_t index = 0;

            if (text[0] == '-') {

                if (!numberParams.allowNegative) {
                    return false;
                }

                index = 1;

                if (text.size() == 1) {
                    return true;
                }
            }

            for (; index < text.size(); index++) {

                char character = text[index];

                if (character == '.') {

                    if (!numberParams.allowDecimal || seenDot) {
                        return false;
                    }

                    seenDot = true;
                    continue;
                }

                if (!std::isdigit((unsigned char)character)) {
                    return false;
                }

                // Note: we do NOT reject extra decimal places while typing;
                // the value is rounded to maxDecimalPlaces on commit.
            }

            return true;
        }

        bool acceptProposedContent(const std::string& proposed) const override {

            if (!TextInput::acceptProposedContent(proposed)) {
                return false;
            }

            // Only reject text that can never become a valid number.  Range
            // (min/max), decimal-place, and forbidden-value constraints are
            // enforced on commit (commitOnLoseFocus -> interpretValue), NOT per
            // keystroke: rejecting mid-entry blocks legitimate values whose
            // partial form is transiently out of range (e.g. typing "12" when
            // the minimum is 7).
            return isValidPartial(proposed);
        }

        void commitOnLoseFocus(Event& e) {

            std::string raw = text->content.get();

            if (raw.empty()) {

                if (numberParams.allowEmpty) {

                    bool changed = committedValue.has_value();

                    applyDisplay("", std::nullopt, e);

                    if (changed && onValueChange) {
                        onValueChange(e, std::nullopt);
                    }
                }

                else {
                    restoreLastCommitted(e);
                }

                return;
            }

            std::optional<double> ingested = ingestFromText(raw);

            if (!ingested) {
                restoreLastCommitted(e);
                return;
            }

            double interpreted = interpretValue(*ingested, numberParams);
            std::string display = formatForDisplay(interpreted, numberParams);

            bool changed = true;

            if (committedValue) {
                changed = std::fabs(*committedValue - interpreted) > 1e-9;
            }

            applyDisplay(display, interpreted, e);

            if (changed && onValueChange) {
                onValueChange(e, interpreted);
            }
        }

        void commit(Event& e) {
            commitOnLoseFocus(e);
        }

        bool tryGetValue(double& out) const {

            if (committedValue) {
                out = *committedValue;
                return true;
            }

            std::optional<double> parsed = ingestFromText(text->content.get());

            if (!parsed) {
                return false;
            }

            out = interpretValue(*parsed, numberParams);
            return true;
        }

        double valueOr(double fallback) const {

            double out = fallback;

            if (tryGetValue(out)) {
                return out;
            }

            return fallback;
        }

        void setValue(double value, Event* event = nullptr) {

            double interpreted = interpretValue(value, numberParams);
            std::string display = formatForDisplay(interpreted, numberParams);

            bool changed = true;

            if (committedValue) {
                changed = std::fabs(*committedValue - interpreted) > 1e-9;
            }

            text->content = display;
            lastCommittedText = display;
            committedValue = interpreted;

            if (changed && onValueChange && event) {
                onValueChange(*event, interpreted);
            }
        }

        std::optional<double> value() const {
            return committedValue;
        }
    };
}
