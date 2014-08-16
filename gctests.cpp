// Very rudimentary test program for gc.cpp.

#include "gc.cpp"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#ifdef NDEBUG
#error "This program uses assert for testing. There's no point building it with assertions disabled."
#endif

// Test that the GC can at least allocate two objects.
void test_can_allocate_twice() {
    // Allocate one object.
    Object* obj1 = allocate();
    assert(obj1);

    root = obj1;

    // Allocate a second object.  Since obj1 is the root, obj2 must be a
    // different pointer.
    Object* obj2 = allocate();
    assert(obj2);
    assert(obj2 != obj1);

    // Set root to null, indicating that there is no root object anymore.
    // Every test will do this to clean up after itself.  It means "I'm not
    // using any objects anymore; consider them all garbage."
    root = nullptr;
}

// Test that the object pointed to by root is not collected and reused.
void test_root_is_not_recycled() {
    // Create one object and make it the root.
    root = allocate();
    assert(root);

    // Subsequent allocations never return root.
    for (int i = 0; i < HEAP_SIZE * 2; i++) {
        Object* tmp = allocate();
        assert(tmp != root);
    }

    root = nullptr;
}

// Helper function to allocate and populate an Object all in one go.  Only call
// this if you're sure allocation will succeed.  If the heap is full and every
// object is reachable, you'll get an assertion failure.
Object* new_object(Object* head, Object* tail)
{
    Object* obj = allocate();
    assert(obj);
    obj->head = head;
    obj->tail = tail;
    return obj;
}

// Test allocate()'s behavior when the heap is full and every Object is
// reachable.
void test_full_heap() {
    // Fill up the heap by allocating HEAP_SIZE objects.
    root = nullptr;
    for (int i = 0; i < HEAP_SIZE; i++)
        root = new_object(nullptr, root);

    // The whole heap is reachable.  Now allocate() should return null every
    // time it's called.
    for (int i = 0; i < 4; i++)
        assert(allocate() == nullptr);
}

// Test allocate()'s behavior when the heap is only almost full.
void test_nearly_full_heap() {
    // Make the heap nearly full by allocating (HEAP_SIZE - 1) objects.
    root = nullptr;
    for (int i = 0; i < HEAP_SIZE - 1; i++)
        root = new_object(nullptr, root);

    // Now the entire heap is reachable except for one Object.  We should be
    // able to call allocate() successfully, repeatedly.  It returns that one
    // object every time it's called!
    Object* last = allocate();
    assert(last);
    for (int i = 0; i < 10; i++)
        assert(allocate() == last);
}

// Helper function used by some of the tests below.  Force garbage collection
// to happen at least once.
void force_gc() {
    // Many GCs expose an API to force GC to happen.  Ours doesn't.  The only
    // way to force GC is to allocate objects until we run out of memory,
    // making sure to keep the original root rooted throughout.
    Object* orig_root = root;
    while (Object* obj = allocate()) {
        obj->tail = root;
        root = obj;
    }

    // When we get here, GC has already happened at least once, and the heap is
    // completely full---every Object is allocated and reachable from the root.

    // Now put the root set back how it was before, and allocate() one more
    // time.  This forces GC to happen again, collecting all the garbage
    // objects we created above.
    root = orig_root;
    allocate();
}

// Test that objects reachable from root->head or ->tail are not collected.
void test_reachable_objects_not_collected() {
    Object* obj1 = root = allocate();
    assert(root);
    Object* obj2 = root->head = allocate();
    assert(root->head);
    Object* obj3 = root->tail = allocate();
    assert(root->tail);
    Object* obj4 = root->head->head = allocate();
    assert(root->head->head);
    Object* obj5 = root->head->tail = allocate();
    assert(root->head->tail);

    force_gc();

    assert(root == obj1);
    assert(root->head == obj2);
    assert(root->tail == obj3);
    assert(root->head->head == obj4);
    assert(root->head->tail == obj5);

    root = nullptr;
}

// Test that the GC is not confused by an object that contains pointers to
// itself.
void test_root_self_references() {
    // Create a root object that contains pointers to itself.
    root = allocate();
    assert(root);
    root->head = root;
    root->tail = root;

    force_gc();

    // After GC, the root object should be unchanged.
    assert(root->head == root);
    assert(root->tail == root);

    root = nullptr;
}

// Test that the GC is not confused by cycles in the reachable object graph.
void test_root_cycle() {
    // Set up obj1 and root to point to each other.
    Object* obj1 = allocate();
    assert(obj1);
    root = obj1;
    Object* obj2 = new_object(obj1, obj1);  // obj2 points to obj1
    obj1->head = obj2;  // and vice versa
    obj1->tail = obj2;

    force_gc();

    // After GC, the two objects are unchanged.
    assert(obj1->head == obj2);
    assert(obj1->tail == obj2);
    assert(obj2->head == obj1);
    assert(obj2->tail == obj1);

    root = nullptr;
}

// Test that the GC is not confused by cycles that are garbage.
void test_unreachable_cycle() {
    // Make a cycle.
    Object* obj1 = allocate();
    root = obj1;
    Object* obj2 = allocate();
    obj2->tail = obj1;
    obj1->tail = obj2;

    // Make the cycle unreachable.
    root = nullptr;

    // Allocation should eventually recycle both objects.
    bool recycled1 = false, recycled2 = false;
    for (int i = 0; i < HEAP_SIZE; i++) {
        root = new_object(nullptr, root);
        if (root == obj1)
            recycled1 = true;
        if (root == obj2)
            recycled2 = true;
    }
    assert(recycled1);
    assert(recycled2);

    root = nullptr;
}

int main() {
    init_heap();

    test_can_allocate_twice();
    test_root_is_not_recycled();
    test_full_heap();
    test_nearly_full_heap();
    test_reachable_objects_not_collected();
    test_root_self_references();
    test_root_cycle();
    test_unreachable_cycle();
    test_nearly_full_heap();

    // Each test contains assertions that abort on failure, so if we get here,
    // all assertions passed.
    puts("Tests passed.");
    return 0;
}
