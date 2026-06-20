module;

export module Machine.App.Machine;

export namespace Machine::App {

    // The Machine system: the eventual home of the dispatcher -- the operation
    // queue, sequencing, and safety interlocks. It owns ORDER, not bytes; the
    // wire belongs to a translation sub-layer it delegates to. Blank for now;
    // the refactor fills it in as a system composed of smaller systems.
    struct Machine {

        // Create
        //--------------------------------------------------

        Machine() {}

        // Destroy
        //--------------------------------------------------

        ~Machine() {}
    };
}
