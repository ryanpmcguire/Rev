module;

export module Dirty;

import <vector>;
import <unordered_set>;
import <functional>;

export namespace Dirty {

    struct DirtyFlag {

        bool dirty = true;

        std::unordered_set<DirtyFlag*> tell;
        std::unordered_set<DirtyFlag*> ask;
        std::function<void()> cleanFunc = []{}; // Default no-op

        DirtyFlag() {}

        ~DirtyFlag() {
            for (DirtyFlag* flag : tell) { flag->ask.erase(this); }
        }

        // Single version
        void tells(DirtyFlag* flag = nullptr) {
            if (!flag) { return; }
            tell.insert(flag);
            flag->asks(this);
        }

        // Vector version
        void tells(std::vector<DirtyFlag*> flags) {
            for (DirtyFlag* flag : flags) { this->tells(flag); }
        }

        // Add downstream element
        void asks(DirtyFlag* flag = nullptr) {
            if (!flag) { return; }
            this->ask.insert(flag);
        }

        void beforeClean(std::function<void()> func) {
            cleanFunc = func;
        }

        void clean() {

            if (!dirty) { return; }

            // Ask all children to clean
            for (DirtyFlag* child : ask) {
                child->clean();
            }

            // Once all children are clean, we can execute onClean
            this->cleanFunc();
            dirty = false;

            // Ask all parents to clean
            for (DirtyFlag* parent : tell) {
                parent->clean();
            }
        }

        // Boolean conversion operator
        operator bool() const {
            return dirty;
        }

        // Assignment operator for bool
        DirtyFlag& operator=(bool value) {

            if (value && !dirty) { for (DirtyFlag* flag : tell) { *flag = true; } } // If we are *now* dirty
            //if (!value && dirty) { for (DirtyFlag* flag : ask) { *flag = false; } } // If we are *now* clean

            dirty = value;
            return *this;
        }
    };
}
