module;

#include <vector>

export module Rev.DirtyFlag;

export namespace Rev {

    struct DirtyFlag {

        bool dirty = true;
        std::vector<DirtyFlag*> upstream;

        // Connect dirty flag to upstream
        void connect(DirtyFlag* flag) {
            for (DirtyFlag* f : upstream) { if (f == flag) { return; } }
            upstream.push_back(flag);
        }
        
        // Disconnect dirty flag from upstream
        void disconnect(DirtyFlag* flag) {
            auto it = std::remove(upstream.begin(), upstream.end(), flag);
            if (it != upstream.end()) { upstream.erase(it, upstream.end()); }
        }

        /*operator bool() const noexcept {
            return dirty;
        }

        DirtyFlag& operator=(bool value) {

            // Set new value if not dirty
            if (dirty) { return *this; }
            dirty = value;

            // Inform entire upstream
            for (DirtyFlag* flag : upstream) {
                if (!flag->dirty) { *flag = true; }
            }
        }     */ 
    };
};