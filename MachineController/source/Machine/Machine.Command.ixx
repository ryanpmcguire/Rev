module;

export module App.Command;

export namespace App {

    // The machine's inward vocabulary: intents the system speaks, lowered to the
    // wire by an adapter. A command is either an ACTION ("do this") or a QUERY
    // ("tell us this") -- a query is just a command whose reply the adapter
    // decodes back into the machine (info / telemetry). Connect/disconnect are
    // lifecycle, not commands.
    struct Command {

        enum class Type {

            // Actions -- do something.
            Unlock,         // clear an alarm                 $X
            Reset,          // soft-reset the controller      ctrl-X
            ChangeTool,     // automatic tool change          M6 T<tool>

            // Queries -- tell us something (the machine replies on the wire).
            QueryStatus,    // live status frame              ?
            QueryOffsets,   // work offsets, TLO, last probe  $#
            QueryState,     // modal / parser state           $G
            QuerySwitches,  // switch (peripheral) states     $S
            QueryVersion    // firmware version               version
        };

        Type type = Type::QueryStatus;
        int  tool = 0;     // ChangeTool: target tool number
    };
}
