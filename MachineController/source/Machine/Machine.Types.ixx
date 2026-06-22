module;

export module Machine.Types;

export namespace Machine {

    // A coordinate / offset (linear X/Y/Z + rotary A,B)
    struct Coord { float x = 0, y = 0, z = 0, a = 0, b = 0; };
}
