Skill file template
---
name: cpp-formatting
description: >-
  Formats C++ code in the Rev/CAM codebase with compact, file-local style.
  Use when writing or editing C++, .ixx modules, headers, or when the user
  mentions formatting, whitespace, indentation, or code style.
---
# C++ formatting (Rev)
Match the **surrounding file**. Prefer compact code over vertically spread-out code.
## Hard rules
1. **4-space indent** only.
2. **One-line signatures** for normal functions/methods.
3. **Do not break simple calls** across lines (`std::find`, `push_back`, short `if` checks).
4. **No blank line before `else`.**
5. **Blank lines sparingly** — between logical sections only, not after every statement or guard.
6. **Short guards stay compact** when they fit on one line.
7. **When editing a file**, copy the density of nearby functions (e.g. `removeChild`, not bloated new code).
## Prefer this
```cpp
void moveChild(Element* child, Element* target, bool before) {
    if (!child || !target || child == target) return;
    if (child->parent != this) return;
    auto childIt = std::find(children.begin(), children.end(), child);
    if (childIt == children.end()) return;
    children.erase(childIt);
    auto targetIt = std::find(children.begin(), children.end(), target);
    if (targetIt == children.end()) {
        children.push_back(child);
        return;
    }
    if (before) children.insert(targetIt, child);
    else children.insert(targetIt + 1, child);
    if (shared && shared->event) child->refresh(*shared->event);
}
Avoid this
void moveChild(
    Element* child,
    Element* target,
    bool before
) {
    if (!child || !target || child == target) {
        return;
    }
    auto childIt = std::find(
        children.begin(),
        children.end(),
        child
    );
    if (before) {
        children.insert(targetIt, child);
    }
    else {
        children.insert(targetIt + 1, child);
    }
}
Anti-patterns
Multi-line signatures for 3 short parameters
Breaking std::find(a, b, c) across lines
Blank line before else
Blank line after every return
Extra blank lines inside short functions
Before finishing
Scan your diff: if a function is much taller than neighbors for the same complexity, tighten it.

## Make it actually apply
1. **Description** — Include trigger terms: `C++`, `.ixx`, `formatting`, `whitespace`, `indentation`.
2. **Auto-invocation** — Omit `disable-model-invocation: true` so the agent can pick it up when editing C++.
3. **Examples** — The good/bad block above matters more than prose rules.
## Optional: user rule for always-on
Skills are loaded when relevant; a **user rule** is always on. Short version:
> When writing C++ in Rev, match local file style. Use compact formatting: one-line signatures, no blank line before else, don't break simple calls across lines, blank lines only between logical sections.
Skill + rule together works well: rule for baseline, skill for examples.
## Even stronger: point at real files
Add to the skill:
```markdown
## Reference functions in this repo
When unsure, match:
- `Rev/src/Element/Element.ixx` — `removeChild`
- `CAM/source/App/App.ixx` — short methods like `selectTool`
That anchors the model to code you already like.

