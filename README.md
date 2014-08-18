# GC in 50 lines

Jason Orendorff  
[Nashville Systems Programming Day](http://nashvillecode.org/)  
August 16, 2014

## Intro

All the stuff in your program has to be represented in memory somehow:
code, data, arguments, variables.
The question of how all that is organized is called **memory management**.

Memory management is about knowing where to put things.
And since your computer&rsquo;s memory is a finite resource,
memory management is also about freeing up memory so it can be reused.

As an application programmer, you don&rsquo;t do memory management
for every single object in your program.
But somebody has to.
The system does it for you. Right?

How does that work?

It turns out garbage collection is of a piece with allocation
and other memory management tasks. Not independent.
The GC we show today will include its own allocator.
You&rsquo;ll see why.



## The live-coding part

### API

When we design software, it&rsquo;s best to start with the public parts.

What public features does a garbage collector provide to the user?

Just two things.

First, it lets you create new objects. That&rsquo;s called allocating memory.
Whenever you make an array, a function, any kind of object
in your higher-level language of choice,
the garbage collector makes that possible.

So in C, the API for creating a new object might look like this:

    Object* allocate() {
        ???
    }

Details to be filled in later. C code calls this function,
and it returns a pointer to a freshly allocated object.

==> Now. What&rsquo;s the other feature a garbage collector has to expose?

Wrong!

I&rsquo;m guessing you said something like
"free memory" or "deallocate memory" or "perform GC".

That&rsquo;s not a feature.
Rather, it could be; we could expose a function called
`gc()`&mdash;Java has something called System.gc()&mdash;but
that&rsquo;s not something users want.
What users want is to allocate objects and do stuff with them.
*GC should just happen automatically,* as needed,
and that&rsquo;s how our system will work.

OK. So I did say there were two things a GC allocator has to expose.
The second one is this:
It has to give the user a way to protect objects from being collected.

==> What do you think happens if an object gets garbage-collected
while your program is still using it?

Yeah, in short that would be bad.

So then,

==> How does the garbage collector know which objects are garbage
and which are not?

You&rsquo;ve probably never once thought about this,
because in all garbage-collected languages,
the garbage collector knows about every variable.

That is, when you go to run some JavaScript or Erlang or C# code,
all global and local variables are stored in some kind of data structure
and the GC contains code to walk that data structure
so that the GC can examine every variable.
If it sees a variable that points to a pony object, it says,
aha, that pony is still in use; I&rsquo;d better not collect it.
Every garbage-collected language stores information *somewhere*
about all variables
for the benefit of the collector.

C++ does not.

C++ has no feature "list all the variables" which the GC can use
to inspect variables.

So to keep things as simple as possible,
what we&rsquo;ll do is make a single variable

    Object* root = nullptr;

we&rsquo;ll call it "root", for reasons that will become clear later,
and this variable will be special to the GC.

nullptr, by the way, is one of the C++ ways to spell null.

It starts out null, but the program can store
a pointer to any object in this variable,
and when GC happens, *that* object will be safe.
It won&rsquo;t be collected.
And of course if that object contains a reference to another object,
well, the program may access that object too ---
so we also leave *that* object alone. And so on.

Every other object is fair game to be collected.

This means the application has one rule that it must follow.
Whenever it calls allocate(), any Object it cares about,
any Object it is ever going to use again,
better be reachable from root.

==> Why?

Because everything else is going to be blown away.

So these are the two public things that a GC provides.
A way to allocate objects,
and a way to protect those objects from being collected.

    Object* root = NULL;

    Object* allocate() {
        ???
    }

Actually there is just one more thing that our GC will provide,
and that&rsquo;s a setup function. But we&rsquo;ll get to that later.


### Object

Next, let&rsquo;s define what an object is.

    struct Object {

We should probably start simple.
The simplest thing that you could possibly build a program with is this:

    struct Object {
        Object* head;
        Object* tail;
    };

An object is simply a record containing two pointers to other objects.

A typical dynamic language actually has many types of objects of varying sizes,
and all kinds of bells and whistles around objects and object allocation.

But you&rsquo;d be surprised how much you can do with just two pointers.
If you&rsquo;ve used Lisp, you probably recognize this as a cons cell.
You can build linked lists and binary trees out of it.
A function in Lisp is also easily implemented as
a garbage-collected record containing two pointers:
one pointer to the code for the function,
and one pointer to the environment,
that is, all the variables the function closes over.

So let&rsquo;s go with this for now.

OK. We will expose one more function in our garbage collector,
and that&rsquo;s just a setup function to set everything up.

    void init_heap() {
        ???
    }

This completes the API.
This is everything that&rsquo;s public,
everything the application is allowed to see and use.

    struct Object {
        Object* head;
        Object* tail;
    };

    Object* root = NULL;

    void init_heap() {
        ???
    }

    Object* allocate() {
        ???
    }

Everything we add from now on is "private"&mdash;implementation details.



### Allocation, the fast path

All right.
Now is the part where we start implementing the internals of the GC.
And first of all I want to show you where all these Objects that we&rsquo;re going to manage
are coming from.

    const size_t HEAP_SIZE = 10000;
    Object heap[HEAP_SIZE];

We&rsquo;ll just declare one huge global array of ten thousand objects.
Or ten million, whatever. It doesn&rsquo;t matter.
This means it&rsquo;s the operating system&rsquo;s job
to give us one big slab of memory.
Operating systems are good at that.
*Our* job is to parcel this out to the application, one Object per allocate() call,
and to recycle the Objects that aren&rsquo;t being used anymore.

So how does allocation work?
There are a great many ways we could do this.
The design I&rsquo;m going to show you uses what&rsquo;s called a free list.
The key insight here is that
if there&rsquo;s some memory that the application isn&rsquo;t currently using,
that memory&rsquo;s just sitting there,
and the system can use it for whatever it wants.
And we will.

==> Who here knows what a linked list is?

OK. What we&rsquo;re going to do is make a linked list
of all the Objects in memory that have *not* been allocated.
Initially all the Objects in this array will be in one big
linked list.

    void init_heap() {
        for (int i = 0; i < HEAP_SIZE; i++)
            add_to_free_list(&heap[i]);
    }

And when you call allocate, it&rsquo;ll basically just
remove any object from the freelist and return it to you.

Now I want you to do this part for me.

Here&rsquo;s the pointer to the first object in the free list:

    Object* free_list = NULL;

When the process starts up, the free list is empty,
and init_heap() is going to fill it up for us.
We know what a linked list looks like:
    (draw boxes)
Now we need the code for that.

    void add_to_free_list(Object* object) {
        ???
    }

How can we do that? How about this?

    void add_to_free_list(Object* object) {
        free_list = object;
    }

==> What&rsquo;s wrong with this?

That&rsquo;s not adding to the free list, that&rsquo;s clobbering the free list.

    void add_to_free_list(Object* object) {
        object->tail = free_list;
        free_list = object;
    }

Right. That&rsquo;s all!
If you think about this from a performance perspective,
it&rsquo;s one read from memory,
because you&rsquo;re reading a global varaible,
and two writes.
Reading from local variables and arguments is essentially free.

OK. The cool thing now is that our allocate() function
is essentially nothing more or less than the exact opposite
of the code we just wrote!

    Object* allocate() {
        Object* p = free_list;  // grab the first free object
        free_list = p->tail;
        return p;
    }

This is two reads and a write, because symmetry.

And that&rsquo;s it.

    struct Object {
        Object* head;
        Object* tail;
    };

    const int HEAP_SIZE = 10000;
    Object heap[HEAP_SIZE];
    Object* free_list = NULL;
    Object* root = NULL;

    void add_to_free_list(Object* object) {
        object->tail = free_list;
        free_list = object;
    }

    void init_heap() {
        for (int i = 0; i < HEAP_SIZE; i++)
            add_to_free_list(&heap[i]);
    }

    Object* allocate() {
        Object* p = free_list;  // grab the first free object
        free_list = p->tail;
        return p;
    }

A full garbage collecting memory management system
in just 25 lines of code.
Pretty cool huh?
Thank you for coming.


### When to do GC

So obviously there is just one thing missing from this system.
It&rsquo;ll work&mdash;we can actually build a program that uses this,
and call allocate(), and get objects, and run code&mdash;but
eventually the free list becomes empty.

==> Then what will happen?

In allocate(), we need to detect that the free list is empty.

==> Is there a handy way to do that?

Yes, the free_list variable will be null.

So if it&rsquo;s null, that would be a good time to garbage collection,
try and free up some memory.

    Object* allocate() {
        if (free_list == nullptr) {  // out of memory
            // do gc
        }
        Object* p = free_list;
        free_list = p->tail;
        return p;
    }

Of course it&rsquo;s always possible that the application
simply requires more memory than we&rsquo;ve got.
Maybe we do garbage collection and nothing shakes loose.
==> And then what?

That&rsquo;s an out of memory error.
So I&rsquo;ll have allocate() return a null pointer.
Of course there are other things you could do here,
like throw an exception,
or beg the operating system for more memory,
or whatever.

    Object* allocate() {
        if (free_list == nullptr) {  // out of memory
            // do gc
            if (free_list == nullptr)  // still out of memory!
                return nullptr;        // give up :(
        }
        Object* p = free_list;
        free_list = p->tail;
        return p;
    }


### How to do GC

We have reached the garbage collection portion of the program.

The kind of GC I&rsquo;m going to show you is very simple.
It&rsquo;s called a mark and sweep GC.
And it works like this...
(explanation)

To do this, we need an extra bit per object called the "mark bit".
For simplicity&rsquo;s sake, let&rsquo;s just stick it on the Object.

    struct Object {
        Object* head;
        Object* tail;
        bool marked;
    };

==> How much memory does this cost?

OK. So our scheme is going to be,
first, make sure the mark bit is set to false for all objects,
second, mark all objects that the application is still using,
third, collect all objects that aren&rsquo;t marked.

First part first:

            for (int i = 0; i < HEAP_SIZE; i++)       // 1.  clear all mark bits
                heap[i].marked = false;

Then the mark part. I&rsquo;m going to make a function to do this for us,
so we only supply the root and it does the whole "flood fill" part.

            mark(root);                               // 2.  mark phase

Lastly, the sweeping part. That&rsquo;s easy too:

            for (int i = 0; i < HEAP_SIZE; i++)       // 3.  sweep phase
                if (!heap[i].marked)
                    add_to_free_list(&heap[i]);

With that, our allocate function is complete!

    Object* allocate() {
        if (free_list == nullptr) {                   // out of memory, need gc
            for (int i = 0; i < HEAP_SIZE; i++)       // 1.  clear all mark bits
                heap[i].marked = false;
            mark(root);                               // 2.  mark phase
            for (int i = 0; i < HEAP_SIZE; i++)       // 3.  sweep phase
                if (!heap[i].marked)
                    add_to_free_list(&heap[i]);
            if (free_list == nullptr)                 // still out of memory!
                return nullptr;                       // give up :(
        }
        Object* p = free_list;                  // grab the first free object
        free_list = p->tail;                    // remove it from the free list
        return p;
    }

All we need is this mark function that implements the mark phase.
You pass it one object, the root,
and it has to set the mark bit on that object
so that the sweep phase knows not to collect it.

    void mark(Object* obj) {
        obj->marked = true;
    }

==> What&rsquo;s wrong with that?

    void mark(Object* obj) {
        mark(obj->head);
        mark(obj->tail);
        obj->marked = true;
    }

==> What&rsquo;s wrong with that?

    void mark(Object* obj) {
        if (obj == nullptr)
            return;
        mark(obj->head);
        mark(obj->tail);
        obj->marked = true;
    }

==> What&rsquo;s wrong with that?

    void mark(Object* obj) {
        if (obj == nullptr || obj->marked)
            return;
        mark(obj->head);
        mark(obj->tail);
        obj->marked = true;
    }

==> What&rsquo;s wrong with that?

    void mark(Object* obj) {
        if (obj == nullptr || obj->marked)
            return;
        obj->marked = true;
        mark(obj->head);
        mark(obj->tail);
    }

And here&rsquo;s where we stand now.

    struct Object {
        Object* head;
        Object* tail;
    };

    const int HEAP_SIZE = 10000;
    Object heap[HEAP_SIZE];
    Object* free_list = NULL;
    Object* root = NULL;

    void add_to_free_list(Object* object) {
        object->tail = free_list;
        free_list = object;
    }

    void init_heap() {
        for (int i = 0; i < HEAP_SIZE; i++)
            add_to_free_list(&heap[i]);
    }

    void mark(Object* obj) {
        if (obj == nullptr || obj->marked)
            return;
        obj->marked = true;
        mark(obj->head);
        mark(obj->tail);
    }

    Object* allocate() {
        if (free_list == nullptr) {                   // out of memory, need gc
            for (int i = 0; i < HEAP_SIZE; i++)       // 1.  clear all mark bits
                heap[i].marked = false;
            mark(root);                               // 2.  mark phase
            for (int i = 0; i < HEAP_SIZE; i++)       // 3.  sweep phase
                if (!heap[i].marked)
                    add_to_free_list(&heap[i]);
            if (free_list == nullptr)                 // still out of memory!
                return nullptr;                       // give up :(
        }
        Object* p = free_list;                  // grab the first free object
        free_list = p->tail;                    // remove it from the free list
        return p;
    }

We&rsquo;re sitting at about 48 lines right now, which means we&rsquo;re not done.
I suppose we could add some documentation.

    // gc.cpp - A simplistic GC in 50 lines of code (use init_heap, allocate, and root)

There&rsquo;s still one lovely little bug in here, which is that if you allocate an object,
then try to allocate another object, the second allocation fails!

==> Why? How can we fix that?

(The bug is that the allocate() returns an object that entrains the entire free list.
The fix is to null out p->tail before returning.)



## Toy GC vs. real GC

Now that we&rsquo;ve seen a garbage collector in just 50 lines of code,
the question arises, are all garbage collectors this simple?

The answer is no... garbage collectors are some of the most complex
software we make. A GC can be thousands of lines of code.

Why is this garbage collector not serious?
What&rsquo;s wrong with it?

*   **Objects are just two pointers.**
    This is obviously pretty limiting, but now that we have it working,
    you can add whatever other fields you want to `struct Object`.
    You can add an int here, and a float there,
    whatever your application needs,
    and the GC will continue to work just fine.
    So that&rsquo;s not really a problem.

*   **All objects have to be the same size.**
    This is a problem.
    Real applications have objects of different sizes, obviously.
    A common solution to this is for the garbage collector
    to have several "size classes",
    that is, instead of the heap being one big array of objects of the same size,
    and one big freelist,
    the heap would contain a few different arrays
    for objects of different sizes.
    Each different size also gets its own freelist.

    (Note that "size classes" are not the same thing as the things
    you declare using the `class` keyword in object-oriented languages.)

    There are other approaches, too.

*   **The mark bit wastes 63 bits per object.**
    If that doesn&rsquo;t sound like a lot,
    that just means you&rsquo;re not a systems programmer.
    A real implementation might have separate pages just for mark bits,
    with the bits all packed together, 8 bits per byte, no waste.

    Or it might be possible to find a spare bit in the object.
    For example, we&rsquo;ve got two pointers fields, and it turns out
    not every bit of a pointer value is really used on x86-64.
    So there are maybe 38 bits in this struct that are always zero.
    You could use one of those as the mark bit.

*   **The heap size is fixed.**
    A real program would start out by saying, hello operating system,
    can I have 16 megabytes please? I want to make some objects.
    And that would become your initial heap.

    Then, as the application runs, and the heap fills up,
    and the GC notices that pretty much everything is live
    and it&rsquo;s not able to collect a lot of objects each time GC happens,
    it can just ask the operating system for more memory.

*   **Marking is implemented recursively.**
    This means if you create a long linked list,
    the next GC will overflow the stack and you&rsquo;ll crash.

    (Very deep recursion causes crashes in C++.)

    Fixing this would be a good exercise
    if you really want to cement all this in your memory.
    There&rsquo;s a standard trick for converting recursive algorithms to iterative ones,
    using an explicit stack;
    alternatively, there&rsquo;s a nonobvious trick that involves reversing pointers.

*   **There is only one root.**

    Real garbage collectors have a "root set"
    consisting of *all* the objects the program still needs.
    The root set needs to contain all local variables
    and all global or static variables
    that point to objects.

    But local variables are created and go out of scope all the time.
    How does the garbage collector keep track of that?

    The answer is, they have to integrate with the compiler or interpreter
    of the programming language they serve.
    The language and the garbage collector literally have to coordinate
    just so the GC has a way to compute the root set.

    C++ is a huge pain because the C++ compiler
    does not coordinate with GC at all.
    Why would it? C++ doesn&rsquo;t have any built-in GC.
    So you really don&rsquo;t want a big C++ codebase using a GC,
    because if the compiler won&rsquo;t integrate with the GC,
    how do you get a root set?
    Guess what. If the compiler won&rsquo;t do it for you,
    user code has to tell the GC what it&rsquo;s doing,
    which variables it&rsquo;s using.
    This means you end up using smart pointer classes
    for all pointers from application code to GC-allocated objects.
    It&rsquo;s an incredible pain,
    but this is what V8 does, this is what Firefox does.

*   **There&rsquo;s no instrumentation.**
    A real GC is full of code to measure its own performance
    as it&rsquo;s being used in a real program.
    How often is GC happening? How long does it take?
    How many objects are reclaimed each time?

    This information is really useful
    when you&rsquo;re trying to make GC as smooth and fast as possible.

*   **It&rsquo;s slow.**
    OK, now we get to the nitty-gritty.

    This is what&rsquo;s called a stop-the-world garbage collector.
    This allocate function is normally very fast,
    but whenever it determines that GC is needed,
    it walks the entire live object graph,
    then reads and writes every single object in the entire heap.
    So there will be long GC pauses.
    The bigger you make the heap, the longer the pauses.

    This GC only has ten thousand objects in the heap.
    On my laptop, this GC takes up to 100 microseconds to run.
    That&rsquo;s really fast. That&rsquo;s a tenth of a millisecond.
    But now scale it up.
    Say we had a larger heap, with two million objects in it.
    GC would be 20 milliseconds.
    If you do that, no matter how rare it is,
    in a game that&rsquo;s trying to maintain 60 frames per second,
    you&rsquo;ve just dropped a frame.
    Now think about the same GC running on your phone.

    Performance is the reason GC is a whole field of study,
    there are books about it,
    at Mozilla we have a GC team.
    How do we make this fast?

    So there are a couple of techniques that are
    kind of the current state of the art.

    **Incremental GC** spreads out the work so it doesn&rsquo;t happen all at once.
    This can save you from dropping frames.
    The user doesn&rsquo;t notice GC pauses
    if each pause is individually very short.

    **Generational GC** is harder to explain.
    It takes advantage of the weird fact that in a modern language,
    most objects are extremely short-lived.
    This means if you focus on just the most recently allocated objects,
    often you can reclaim a bunch of memory
    without having to mark and sweep the entire heap.
    That saves a lot of time.

    These two techniques can be used together.
    Both are well-understood
    and have many high-quality implementations.
    But it&rsquo;s tricky stuff.
    Some amazing cleverness is required to get these techniques to be
    both correct and fast.

Thanks for taking this trip through a simple garbage collector with me.

I like this example because it&rsquo;s short but it&rsquo;s also packed with cool stuff,
you&rsquo;ve got linked lists, recursion, pointers, graph algorithms&mdash;
it&rsquo;s rather wonderful that all this cool stuff turns out to be so useful.



## Running the code

You can get the code and build it like this:

    git clone git@github.com:jorendorff/gc-in-50-lines.git
    cd gc-in-50-lines
    make
    ./gctests

It works.
But the magic is in the reading and thinking,
and not so much the running.
