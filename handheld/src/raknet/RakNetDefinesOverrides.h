// USER EDITABLE FILE

// 3DS / NDS: тиновые стеки потоков. Не даём alloca() рвать стек —
// большие буферы пойдут через new[]/free.
#if defined(__3DS__) || defined(__NDS__)
#define MAX_ALLOCA_STACK_ALLOCATION 4096
#endif
