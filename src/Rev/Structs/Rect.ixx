module;

export module Rev.Rect;

import Rev.Pos;

export namespace Rev {

    struct Rect {

        float x, y;
        float w, h;

        inline bool contains(Pos& pos)
        {
            return (
                pos.x > x && pos.x < x + w &&
                pos.y > y && pos.y < y + h
            );
        }
    };
};