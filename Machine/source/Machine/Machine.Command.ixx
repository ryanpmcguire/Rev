module;

export module Machine.App.Command;

export namespace App {

    // The machine's inward vocabulary: intents the system speaks, lowered to the
    // wire by an adapter. (Connect/disconnect are lifecycle, not commands.)
    struct Command {

        enum class Type {
            Unlock,        // clear an alarm ($X)
            Reset,         // soft-reset the controller
            QueryStatus    // request a status frame
        };

        Type type = Type::QueryStatus;
    };
}
