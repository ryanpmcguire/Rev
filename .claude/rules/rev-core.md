# Rev core (preloaded)

The core of the Rev UI framework is small, so it is loaded directly below instead of
being rediscovered each session. Read the overview docs first for the model, then the
source. Treat all of this as known. Paths are relative to this file.

## Overview docs

@../../Rev/src/About/Rev.About.md
@../../Rev/src/About/Rev.Core.md
@../../Rev/src/About/Rev.Style.md
@../../Rev/src/Element/About/Element.About.md
@../../Rev/src/Element/About/Element.Api.md
@../../Rev/src/Element/About/Element.Event.About.md

## Geometry

@../../Rev/src/Core/Geometry/Pos.ixx
@../../Rev/src/Core/Lrtb.ixx
@../../Rev/src/Core/Rect.ixx

## Elements & style

@../../Rev/src/Element/Element.ixx
@../../Rev/src/Element/Style/Style.ixx
@../../Rev/src/Element/Resolved.ixx
@../../Rev/src/Elements/Box.ixx

## Runtime (loop, scheduler, animation, events)

@../../Rev/src/Native/Windows/Application.win.ixx
@../../Rev/src/Core/Process.ixx
@../../Rev/src/Core/Animator.ixx
@../../Rev/src/GlobalTime.ixx
@../../Rev/src/Element/Event/Event.ixx
