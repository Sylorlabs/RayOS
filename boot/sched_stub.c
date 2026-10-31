__attribute__((weak)) void scheduler_idle(unsigned int cpu){ (void)cpu; for(;;) __asm__ volatile("hlt"); }
