// gc.cpp - A simplistic mark-and-sweep garbage collector in 50 lines of C++

struct Object {
    Object* head;
    Object* tail;
    bool marked;
};

const int HEAP_SIZE = 10000;
Object heap[HEAP_SIZE];
Object* root = nullptr;   // compile with -std=c++11 to get 'nullptr'
Object* free_list = nullptr;

void add_to_free_list(Object* p) {
    p->tail = free_list;
    free_list = p;
}

void init_heap() {
    for (int i = 0; i < HEAP_SIZE; i++)
        add_to_free_list(&heap[i]);
}

void mark(Object* p) {  // set the mark bit on p and all its descendants
    if (p == nullptr || p->marked)
        return;
    p->marked = true;
    mark(p->head);
    mark(p->tail);
}

Object* allocate() {
    if (free_list == nullptr) {      // out of memory, do GC
        for (int i = 0; i < HEAP_SIZE; i++)  // 1. clear mark bits
            heap[i].marked = false;
        mark(root);                          // 2. mark phase
        free_list = nullptr;                 // 3. sweep phase
        for (int i = 0; i < HEAP_SIZE; i++)
            if (!heap[i].marked)
                add_to_free_list(&heap[i]);
        if (free_list == nullptr)
            return nullptr;          // still out of memory :(
    }
    Object* p = free_list;
    free_list = free_list->tail;
    p->head = nullptr;
    p->tail = nullptr;
    return p;
}
