/* boot/proc.c — minimal multi-process support: build an isolated address space
   for a flashz binary (its own CR3; private frames for text/data/stack; kernel
   regions shared identity) and run it on an AP core. Proves true parallel
   multi-process execution — the desktop on the BSP, a worker per AP core. */
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;

extern u64 k_table(void);
extern u64 k_frame2m(void);
extern void ap_prepare(void);
extern u8 worker_blob[], worker_blob_end[];

static void pcopy(u8*d,const u8*s,u64 n){ for(u64 i=0;i<n;i++) d[i]=s[i]; }

struct proc { u64 cr3, entry, rsp; };

static struct proc proc_create(char core){
    u64 pml4=k_table(); u64* P=(u64*)pml4;
    u64 pdpt=k_table(); u64* D=(u64*)pdpt;
    u64 pd[4]; for(int i=0;i<4;i++) pd[i]=k_table();
    for(int i=0;i<512;i++){ P[i]=0; D[i]=0; }
    P[0]=pdpt|3;
    for(int i=0;i<4;i++) D[i]=pd[i]|3;
    for(int i=0;i<2048;i++) ((u64*)pd[i/512])[i%512] = ((u64)i<<21)|0x83;   /* identity 4GiB */

    u64 ftext=k_frame2m(), fdata=k_frame2m(), fstack=k_frame2m();
    /* load the worker ELF into the private text/data frames */
    u8* e=worker_blob;
    u64 phoff=*(u64*)(e+0x20); u16 phn=*(u16*)(e+0x38), phs=*(u16*)(e+0x36);
    for(u16 i=0;i<phn;i++){ u8* ph=e+phoff+(u64)i*phs; if(*(u32*)ph!=1) continue;
        u64 off=*(u64*)(ph+8), va=*(u64*)(ph+0x10), fsz=*(u64*)(ph+0x20);
        u64 dst=(va==0x400000)? ftext : fdata;
        pcopy((u8*)dst, e+off, fsz);
    }
    /* stamp the core id into the worker's private data page (VA 0x4100000) */
    ((u8*)(fdata+0x100000))[0]=(u8)core;
    /* remap the worker's private VAs (all in pd[0], 0..1GiB) */
    ((u64*)pd[0])[0x400000 >>21] = ftext |0x83;   /* text  @0x400000  */
    ((u64*)pd[0])[0x4000000>>21] = fdata |0x83;   /* data  @0x4000000 */
    ((u64*)pd[0])[0x3000000>>21] = fstack|0x83;   /* stack @0x3000000 */
    struct proc pr; pr.cr3=pml4; pr.entry=0x4000b0; pr.rsp=0x3000000+0x1FFFF0;
    return pr;
}

/* called by each AP (smp.c) after it comes online */
void ap_run(u32 core){
    ap_prepare();                                  /* per-CPU GDT/CS/IDT/SSE/syscall */
    struct proc pr = proc_create((char)('0'+(core%10)));
    __asm__ volatile("mov %0,%%cr3"::"r"(pr.cr3):"memory");
    __asm__ volatile("mov %0,%%rsp\n\t xor %%rbp,%%rbp\n\t jmp *%1"::"r"(pr.rsp),"r"(pr.entry));
    for(;;) __asm__ volatile("hlt");
}
