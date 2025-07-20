module;

export module Rev.Primitive;

export namespace Rev {

    struct Primitive {

        // Create
        Primitive() {

        }

        // Destroy
        ~Primitive() {
            
        }

        // Record commands
        virtual void record() {

        }

        // Sync primitive data
        virtual void sync() {

        }
    };
};