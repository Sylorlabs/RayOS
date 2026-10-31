/* boot/kmain64.c — 64-bit long-mode payload + full syscall shim (Milestone 3).
   Runs the flashz-compiled RayOS kernel UNMODIFIED bare-metal by emulating the
   Linux syscalls it uses: real page-frame mmap (with on-demand paging so the
   high hw_globals VA and chamber VAs get backed by RAM), open/close/read/write/
   ftruncate/getpid/nanosleep/time/rename/exit. Framebuffer output: the kernel
   writes rendered frames as P6 PPM to *.ppm fds — we parse and blit them
   straight to the linear framebuffer. No Linux, no files, no bootloader FB. */
typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;

/* ── COM1 serial ────────────────────────────────────────────────────────── */
static inline void outb(u32 p,u8 v){ __asm__ volatile("outb %0,%w1"::"a"(v),"Nd"(p)); }
static inline u8   inb (u32 p){ u8 r; __asm__ volatile("inb %w1,%0":"=a"(r):"Nd"(p)); return r; }
static void sputc(char c){ while((inb(0x3F8+5)&0x20)==0){} outb(0x3F8,(u8)c); }
static void sputs(const char*s){ while(*s) sputc(*s++); }
static void sputu(u32 v){ char b[16]; int i=0; if(!v){sputc('0');return;}
    while(v){b[i++]='0'+v%10; v/=10;} while(i) sputc(b[--i]); }
static void sputx64(u64 v){ sputs("0x"); for(int s=60;s>=0;s-=4){ u8 n=(v>>s)&0xF;
    sputc(n<10?'0'+n:'a'+n-10);} }

/* ── exception IDT (diagnostics) ────────────────────────────────────────── */
struct idt_entry { u16 off_lo; u16 sel; u8 ist; u8 attr; u16 off_mid; u32 off_hi; u32 zero; } __attribute__((packed));
static struct idt_entry g_idt[48];
static struct idt_entry g_idt_ap[32];
static struct { u16 limit; u64 base; } __attribute__((packed)) g_idtr_ap;
static struct { u16 limit; u64 base; } __attribute__((packed)) g_idtr;
extern u64 isr_table[32];
extern void irq_timer(void); extern void irq_keyboard(void); extern void irq_mouse(void);
static const char* g_exc_names[20] = {
    "#DE","#DB","NMI","#BP","#OF","#BR","#UD","#NM","#DF","CSO",
    "#TS","#NP","#SS","#GP","#PF","---","#MF","#AC","#MC","#XM" };
void exc_handler(u64 vec,u64 err,u64 rip,u64 cr2);   /* fwd (defined below sputs) */
static void set_gate(int i,u64 h,u8 ist){
    g_idt[i].off_lo=(u16)h; g_idt[i].sel=0x08; g_idt[i].ist=ist; g_idt[i].attr=0x8E;
    g_idt[i].off_mid=(u16)(h>>16); g_idt[i].off_hi=(u32)(h>>32); g_idt[i].zero=0;
}
static void build_idt(void){
    for(int i=0;i<32;i++) set_gate(i, isr_table[i], 1);        /* exceptions on IST1 */
    set_gate(32, (u64)&irq_timer,    1);                       /* IRQ0  PIT       */
    set_gate(33, (u64)&irq_keyboard, 1);                       /* IRQ1  keyboard  */
    set_gate(44, (u64)&irq_mouse,    1);                       /* IRQ12 mouse     */
    for(int i=0;i<32;i++){ g_idt_ap[i]=g_idt[i]; g_idt_ap[i].ist=0; }
    g_idtr_ap.limit=sizeof(g_idt_ap)-1; g_idtr_ap.base=(u64)g_idt_ap;
    g_idtr.limit=sizeof(g_idt)-1; g_idtr.base=(u64)g_idt;
    __asm__ volatile("lidt %0"::"m"(g_idtr));
}

/* ── ATA PIO block driver (LBA28) + persistence demo ────────────────────── */
static inline void outw(u32 p,u16 v){ __asm__ volatile("outw %0,%w1"::"a"(v),"Nd"(p)); }
static inline u16 inw (u32 p){ u16 r; __asm__ volatile("inw %w1,%0":"=a"(r):"Nd"(p)); return r; }
static int ata_wait(void){ int t=1000000; while(t-- && (inb(0x1F7)&0x80)){} return t>0; }
static int ata_present(void){ return inb(0x1F7)!=0xFF; }   /* 0xFF = no drive on the bus */
static void ata_lba(u32 lba){
    outb(0x1F6, 0xE0 | ((lba>>24)&0x0F)); outb(0x1F1,0); outb(0x1F2,1);
    outb(0x1F3, lba&0xFF); outb(0x1F4,(lba>>8)&0xFF); outb(0x1F5,(lba>>16)&0xFF);
}
static void ata_read(u32 lba, u8* buf){
    ata_wait(); ata_lba(lba); outb(0x1F7,0x20);            /* READ SECTORS */
    ata_wait(); int t=1000000; while(t-- && !(inb(0x1F7)&0x08)){}
    u16* w=(u16*)buf; for(int i=0;i<256;i++) w[i]=inw(0x1F0);
}
static void ata_write(u32 lba, const u8* buf){
    ata_wait(); ata_lba(lba); outb(0x1F7,0x30);            /* WRITE SECTORS */
    ata_wait(); int t=1000000; while(t-- && !(inb(0x1F7)&0x08)){}
    const u16* w=(const u16*)buf; for(int i=0;i<256;i++) outw(0x1F0,w[i]);
    outb(0x1F7,0xE7); ata_wait();                          /* CACHE FLUSH */
}
/* Prove persistence: a boot counter in LBA 40 (free padding) that survives
   reboot — real read/modify/write to the same disk we booted from. */
static void persistence_demo(void){
    if(!ata_present()){ sputs("[RayOS] no ATA disk; storage demo skipped\r\n"); return; }
    static u8 sec[512];
    ata_read(40, sec);
    u32* m=(u32*)sec; u32 count;
    if(m[0]==0x52415953u) count=m[1]+1; else { m[0]=0x52415953u; count=1; }
    m[1]=count; ata_write(40, sec);
    sputs("[RayOS] persistent boot count = "); sputu(count);
    sputs(" (read+incremented+written to the ATA disk; survives reboot)\r\n");
}

/* ── ACPI power: shutdown (S5) + reboot ─────────────────────────────────── */
static void acpi_poweroff(void){
    /* QEMU/Bochs/VBox ACPI PM1a control ports (SLP_EN|SLP_TYP). On real
       hardware a full FADT+DSDT _S5 parse is needed; these cover the VMs. */
    outw(0x604, 0x2000);          /* modern QEMU */
    outw(0xB004, 0x2000);         /* older QEMU / Bochs */
    outw(0x4004, 0x3400);         /* VirtualBox */
    for(;;) __asm__ volatile("cli; hlt");
}
static void acpi_reboot(void){
    int t=100000; while(t-- && (inb(0x64)&0x02)){}   /* wait input buffer clear */
    outb(0x64, 0xFE);             /* 8042 pulse reset line */
    /* fallback: triple fault via a null IDT */
    struct { u16 l; u64 b; } __attribute__((packed)) z = {0,0};
    __asm__ volatile("lidt %0; int3"::"m"(z));
    for(;;) __asm__ volatile("cli; hlt");
}

/* ── SSE/SSE2 enable (flashz emits SSE for f32/f64 math) ─────────────────── */
static void enable_sse(void){
    u64 cr0; __asm__ volatile("mov %%cr0,%0":"=r"(cr0));
    cr0 &= ~(1ULL<<2);                 /* clear EM (no x87 emulation) */
    cr0 |=  (1ULL<<1);                 /* set MP */
    __asm__ volatile("mov %0,%%cr0"::"r"(cr0));
    u64 cr4; __asm__ volatile("mov %%cr4,%0":"=r"(cr4));
    cr4 |= (1ULL<<9)|(1ULL<<10);       /* OSFXSR | OSXMMEXCPT */
    __asm__ volatile("mov %0,%%cr4"::"r"(cr4));
    __asm__ volatile("fninit");
}

/* ── SYSCALL MSR setup ──────────────────────────────────────────────────── */
static void wrmsr(u32 msr,u32 lo,u32 hi){ __asm__ volatile("wrmsr"::"c"(msr),"a"(lo),"d"(hi)); }
static void rdmsr(u32 msr,u32*lo,u32*hi){ __asm__ volatile("rdmsr":"=a"(*lo),"=d"(*hi):"c"(msr)); }
extern void syscall_entry(void);
static void setup_syscall(void){
    u32 lo,hi;
    rdmsr(0xC0000080u,&lo,&hi); lo|=1; wrmsr(0xC0000080u,lo,hi);   /* EFER.SCE */
    wrmsr(0xC0000081u, 0, 0x00000008u);                            /* STAR: syscall CS=0x08 */
    u64 h=(u64)&syscall_entry; wrmsr(0xC0000082u,(u32)h,(u32)(h>>32)); /* LSTAR */
    wrmsr(0xC0000084u, 0x00000400u, 0);                            /* SFMASK: clear DF (keep IF so IRQs fire in syscalls) */
}

void exc_handler(u64 vec,u64 err,u64 rip,u64 cr2){
    sputs("\r\n[RayOS] *** CPU EXCEPTION ");
    if(vec<20) sputs(g_exc_names[vec]); else sputu((u32)vec);
    sputs(" vec="); sputu((u32)vec); sputs(" err="); sputx64(err);
    sputs(" rip="); sputx64(rip); sputs(" cr2="); sputx64(cr2); sputs("\r\n");
    for(;;) __asm__ volatile("cli; hlt");
}

/* ── memory + paging ────────────────────────────────────────────────────── */
static void kmemcpy(u8*d,const u8*s,u64 n){ for(u64 i=0;i<n;i++) d[i]=s[i]; }
static void kmemset(u8*d,u8 v,u64 n){ for(u64 i=0;i<n;i++) d[i]=v; }
static u64  read_cr3(void){ u64 v; __asm__ volatile("mov %%cr3,%0":"=r"(v)); return v & ~0xFFFULL; }
static void invlpg(u64 va){ __asm__ volatile("invlpg (%0)"::"r"(va):"memory"); }

volatile int g_memlock=0;
static void memlock(void){ while(__sync_lock_test_and_set(&g_memlock,1)) __asm__ volatile("pause"); }
static void memunlock(void){ __sync_lock_release(&g_memlock); }

/* Fixed RAM layout (needs QEMU -m 2048; flashz kernel occupies ~4-64MiB):
     0x10000000..0x10800000  page-table pages (8MiB)
     0x11000000..0x18000000  2MiB frames for backing non-identity (PCI-hole/high) VAs
     0x18000000..0x78000000  identity RAM heap for anonymous/file maps (VA==phys) */
static u64 tbl_next = 0x10000000ULL; static const u64 tbl_end = 0x10800000ULL;
static u64 alloc_table(void){ memlock(); u64 t=tbl_next; tbl_next+=0x1000ULL; memunlock();
    if(t>=tbl_end){ sputs("[RayOS] FATAL: out of table pages\r\n"); for(;;)__asm__ volatile("hlt"); }
    for(int i=0;i<512;i++) ((u64*)t)[i]=0; return t; }
static u64 frame_next = 0x11000000ULL; static const u64 frame_end = 0x18000000ULL;
static u64 alloc_frame2m(void){ memlock(); u64 f=frame_next; frame_next+=0x200000ULL; memunlock();
    if(f>=frame_end){ sputs("[RayOS] FATAL: out of RAM frames\r\n"); for(;;)__asm__ volatile("hlt"); }
    return f; }
static u64 heap_next = 0x18000000ULL; static const u64 heap_end = 0x78000000ULL;
struct freeblk { u64 addr, size; };
static struct freeblk g_free[1024]; static int g_free_n=0;
static u64 alloc_heap(u64 len){
    u64 sz=(len+0xFFFULL)&~0xFFFULL; if(!sz) sz=0x1000;
    memlock();
    for(int i=0;i<g_free_n;i++){                       /* first-fit reuse of freed blocks */
        if(g_free[i].size>=sz){
            u64 p=g_free[i].addr;
            if(g_free[i].size==sz){ g_free[i]=g_free[--g_free_n]; }
            else { g_free[i].addr+=sz; g_free[i].size-=sz; }
            memunlock(); return p;
        }
    }
    u64 p=heap_next; heap_next+=sz; memunlock();
    if(heap_next>=heap_end){ sputs("[RayOS] FATAL: out of heap\r\n"); for(;;)__asm__ volatile("hlt"); }
    return p; }
u64 k_table(void){ return alloc_table(); }
u64 k_frame2m(void){ return alloc_frame2m(); }
volatile int g_iolock=0;
void iolock(void){ while(__sync_lock_test_and_set(&g_iolock,1)) __asm__ volatile("pause"); }
void iounlock(void){ __sync_lock_release(&g_iolock); }

static void free_heap(u64 addr,u64 len){
    if(addr<0x18000000ULL || addr>=heap_end) return;   /* only free real heap blocks */
    u64 sz=(len+0xFFFULL)&~0xFFFULL; if(!sz) return;
    memlock(); if(g_free_n<1024){ g_free[g_free_n].addr=addr; g_free[g_free_n].size=sz; g_free_n++; } memunlock();
}

/* map one 2MiB page VA->frame (rw, supervisor), creating tables on demand */
static void map_2m(u64 va,u64 frame){
    u64* e=&((u64*)read_cr3())[(va>>39)&0x1FF];
    if(!(*e&1)) *e = alloc_table()|0x3;
    e=&((u64*)(*e & ~0xFFFULL))[(va>>30)&0x1FF];
    if(!(*e&1)) *e = alloc_table()|0x3;
    e=&((u64*)(*e & ~0xFFFULL))[(va>>21)&0x1FF];
    *e = (frame & ~0x1FFFFFULL) | 0x83;      /* present|write|PS(2MiB) */
    invlpg(va);
}

/* ── mmap ───────────────────────────────────────────────────────────────── */
#define MAP_FIXED           0x10
#define MAP_FIXED_NOREPLACE 0x100000
#define RAM_TOP             0x80000000ULL     /* 2GiB: identity <this is real RAM */
static u64 mapped[256]; static int mapped_n=0;
static int is_mapped(u64 p){ for(int i=0;i<mapped_n;i++) if(mapped[i]==p) return 1; return 0; }
static void mark(u64 p){ if(mapped_n<256) mapped[mapped_n++]=p; }

static long sys_mmap(u64 addr,u64 len,u64 flags){
    if(flags & (MAP_FIXED|MAP_FIXED_NOREPLACE)){
        u64 base=addr & ~0x1FFFFFULL;
        u64 span=(addr-base)+len;
        u64 npages=(span+0x1FFFFFULL)>>21; if(!npages) npages=1;
        if((flags&MAP_FIXED_NOREPLACE) && is_mapped(base)) return -17;   /* EEXIST */
        for(u64 i=0;i<npages;i++){ u64 va=base+i*0x200000ULL;
            if(is_mapped(va)) continue;
            if(va >= RAM_TOP) map_2m(va, alloc_frame2m());  /* PCI-hole/high -> RAM frame */
            mark(va);                                        /* else identity RAM, already usable */
        }
        return (long)addr;
    }
    return (long)alloc_heap(len);                            /* anonymous/file -> identity RAM heap */
}

/* ── files: virtual fds; *.ppm fds blit to the framebuffer ──────────────── */
static u8 fd_type[64];   /* 0 free, 2 ppm-sink, 3 generic-sink */
static int next_fd=3;
static int contains(const char*h,const char*n){
    for(const char*p=h;*p;p++){ const char*a=p,*b=n;
        while(*b && *a==*b){a++;b++;} if(!*b) return 1; } return 0; }

/* ── software mouse cursor (save-under overlay, drawn independent of frames) ── */
#define CUR_W 11
#define CUR_H 17
static const char* g_cursor[CUR_H] = {
"#          ","##         ","#.#        ","#..#       ","#...#      ",
"#....#     ","#.....#    ","#......#   ","#.......#  ","#........# ",
"#....######","#..#..#    ","#.# #..#   ","##  #..#   ","#    #..#  ",
"     #..#  ","      ##   " };
static u32 g_cur_save[CUR_W*CUR_H];
static int g_cur_x=640, g_cur_y=400, g_cur_shown=0;
/* the 1280x800 desktop canvas is centered in the (possibly larger) VESA
   framebuffer; input stays in canvas space, only rendering adds this offset */
static int g_off_x=0, g_off_y=0;

static void cursor_restore(void){
    if(!g_cur_shown) return;
    volatile u32* vi=(volatile u32*)0x5000;
    u8* fb=(u8*)(u64)vi[0]; u32 pitch=vi[3],w=vi[1],h=vi[2];
    for(int r=0;r<CUR_H;r++) for(int c=0;c<CUR_W;c++){
        int x=g_cur_x+c+g_off_x,y=g_cur_y+r+g_off_y;
        if(x>=0&&(u32)x<w&&y>=0&&(u32)y<h){
            u8* p=fb+(u64)y*pitch+(u64)x*4; u32 v=g_cur_save[r*CUR_W+c];
            p[0]=v&0xFF; p[1]=(v>>8)&0xFF; p[2]=(v>>16)&0xFF;
        }
    }
    g_cur_shown=0;
}
static void cursor_draw(void){
    volatile u32* vi=(volatile u32*)0x5000;
    u8* fb=(u8*)(u64)vi[0]; u32 pitch=vi[3],w=vi[1],h=vi[2];
    for(int r=0;r<CUR_H;r++) for(int c=0;c<CUR_W;c++){
        int x=g_cur_x+c+g_off_x,y=g_cur_y+r+g_off_y;
        if(x<0||(u32)x>=w||y<0||(u32)y>=h) continue;
        u8* p=fb+(u64)y*pitch+(u64)x*4;
        g_cur_save[r*CUR_W+c]=p[0]|(p[1]<<8)|(p[2]<<16);   /* save-under */
        char ch=g_cursor[r][c];
        if(ch=='#'){ p[0]=0;p[1]=0;p[2]=0; }
        else if(ch=='.'){ p[0]=0xFF;p[1]=0xFF;p[2]=0xFF; }
    }
    g_cur_shown=1;
}
static void cursor_move(int nx,int ny){ cursor_restore(); g_cur_x=nx; g_cur_y=ny; cursor_draw(); }

static void blit_ppm(const u8*buf,u64 len){
    if(len<10 || buf[0]!='P' || buf[1]!='6') return;
    u64 i=2; u32 vals[3]; int vc=0;
    while(vc<3 && i<len){
        while(i<len && (buf[i]==' '||buf[i]=='\n'||buf[i]=='\t'||buf[i]=='\r')) i++;
        u32 v=0; int got=0;
        while(i<len && buf[i]>='0' && buf[i]<='9'){ v=v*10+(buf[i]-'0'); i++; got=1; }
        if(got) vals[vc++]=v;
    }
    i++;                                        /* single whitespace after maxval */
    u32 w=vals[0],h=vals[1];
    volatile u32* vi=(volatile u32*)0x5000;
    u8* fb=(u8*)(u64)vi[0]; u32 lw=vi[1],lh=vi[2],pitch=vi[3];
    __asm__ volatile("cli");                        /* atomic vs the cursor IRQ */
    for(u32 y=0;y<h && (y+g_off_y)<lh;y++){
        u8* row=fb+(u64)(y+g_off_y)*pitch+(u64)g_off_x*4;
        for(u32 x=0;x<w && (x+g_off_x)<lw;x++){
            u64 o=i+((u64)y*w+x)*3; if(o+2>=len){ y=h; break; }
            u8* p=row+(u64)x*4;
            p[0]=buf[o+2]; p[1]=buf[o+1]; p[2]=buf[o]; p[3]=0;   /* RGB->BGRX */
        }
    }
    g_cur_shown=0; cursor_draw();                   /* frame wiped cursor: redraw on top */
    __asm__ volatile("sti");
}

/* ── interrupts: TSS/IST, PIC, PIT, PS/2 keyboard + mouse ────────────────── */
static u64 g_mmio_base = 0;                 /* actual addr of the mmap'd rayos_mmio.bin */
volatile u64 g_ticks = 0;                   /* PIT ticks @1000Hz (shared with net.c) */
u64 net_alloc(u64 n){ return alloc_heap(n); }
extern void net_init_and_test(void);
extern void smp_init(void);

struct tss64 { u32 r0; u64 rsp0,rsp1,rsp2,r1,ist1,ist2,ist3,ist4,ist5,ist6,ist7,r2; u16 r3,iomap; } __attribute__((packed));
static struct tss64 g_tss;
static u8  g_intstack[16384] __attribute__((aligned(16)));
static u64 g_gdt[5];
static struct { u16 limit; u64 base; } __attribute__((packed)) g_gdtr;

static void setup_tss(void){
    u8* t=(u8*)&g_tss; for(unsigned i=0;i<sizeof(g_tss);i++) t[i]=0;
    g_tss.ist1=(u64)g_intstack+sizeof(g_intstack); g_tss.iomap=sizeof(g_tss);
    g_gdt[0]=0;
    g_gdt[1]=0x00AF9A000000FFFFULL;         /* code64 (0x08) */
    g_gdt[2]=0x00AF92000000FFFFULL;         /* data64 (0x10) */
    u64 base=(u64)&g_tss, limit=sizeof(g_tss)-1;
    g_gdt[3]=(limit & 0xFFFF) | ((base & 0xFFFFFF)<<16) | ((u64)0x89<<40)
           | (((limit>>16)&0xF)<<48) | (((base>>24)&0xFF)<<56);
    g_gdt[4]=(base>>32) & 0xFFFFFFFF;
    g_gdtr.limit=sizeof(g_gdt)-1; g_gdtr.base=(u64)g_gdt;
    __asm__ volatile("lgdt %0"::"m"(g_gdtr));
    /* Reload CS to 0x08 (code64 in our GDT) so it matches regardless of which
       boot path set it (multiboot=0x08, disk/stage2=0x18). Otherwise the first
       IRQ's iretq would restore a stale CS that our GDT reuses for the TSS. */
    __asm__ volatile(
        "lea 1f(%%rip), %%rax\n\t"
        "push $0x08\n\t"
        "push %%rax\n\t"
        "lretq\n\t"
        "1:\n\t" ::: "rax","memory");
    __asm__ volatile(
        "mov $0x10,%%ax\n\t mov %%ax,%%ds\n\t mov %%ax,%%es\n\t"
        "mov %%ax,%%ss\n\t mov %%ax,%%fs\n\t mov %%ax,%%gs\n\t" ::: "ax");
    __asm__ volatile("ltr %%ax"::"a"((u16)0x18));
}

void ap_prepare(void){
    __asm__ volatile("lgdt %0"::"m"(g_gdtr));
    __asm__ volatile("lea 1f(%%rip),%%rax\n\t push $0x08\n\t push %%rax\n\t lretq\n\t 1:\n\t":::"rax","memory");
    __asm__ volatile("mov $0x10,%%ax\n\t mov %%ax,%%ds\n\t mov %%ax,%%es\n\t mov %%ax,%%ss\n\t mov %%ax,%%fs\n\t mov %%ax,%%gs":::"ax");
    __asm__ volatile("lidt %0"::"m"(g_idtr_ap));
    enable_sse();
    setup_syscall();
}

static void pic_remap(void){
    outb(0x20,0x11); outb(0xA0,0x11);       /* ICW1 */
    outb(0x21,0x20); outb(0xA1,0x28);       /* ICW2: master->0x20, slave->0x28 */
    outb(0x21,0x04); outb(0xA1,0x02);       /* ICW3 cascade */
    outb(0x21,0x01); outb(0xA1,0x01);       /* ICW4 8086 */
    outb(0x21, (u8)~((1<<0)|(1<<1)|(1<<2))); /* master: IRQ0,1,2 on */
    outb(0xA1, (u8)~(1<<4));                 /* slave: IRQ12 on */
}
static void pit_init(u32 hz){
    u32 div=1193182u/hz;
    outb(0x43,0x36); outb(0x40,div&0xFF); outb(0x40,(div>>8)&0xFF);
}

/* PS/2 controller helpers */
static void ps2_wait_in(void){ int t=200000; while(t-- && (inb(0x64)&2)){} }
static void ps2_wait_out(void){ int t=200000; while(t-- && !(inb(0x64)&1)){} }
static void mouse_cmd(u8 v){ ps2_wait_in(); outb(0x64,0xD4); ps2_wait_in(); outb(0x60,v); }
static u8   mouse_ack(void){ ps2_wait_out(); return inb(0x60); }
static void ps2_mouse_init(void){
    ps2_wait_in(); outb(0x64,0xA8);                     /* enable aux port */
    ps2_wait_in(); outb(0x64,0x20);                     /* read config byte */
    ps2_wait_out(); u8 cfg=inb(0x60);
    cfg |= 0x03; cfg &= ~0x20;                          /* IRQ1+IRQ12 on, mouse clock on */
    ps2_wait_in(); outb(0x64,0x60); ps2_wait_in(); outb(0x60,cfg);
    mouse_cmd(0xF6); mouse_ack();                       /* set defaults */
    mouse_cmd(0xF4); mouse_ack();                       /* enable data reporting */
}

/* push an event into the desktop's input ring (mmio+0xC000): head,tail,cap,ev[] */
static void push_input(int kind,int x,int y,int btn){
    if(!g_mmio_base) return;
    volatile u32* r=(volatile u32*)(g_mmio_base+0xC000);
    u32 cap=r[2]; if(cap!=64) return;                   /* not yet initialised by guest */
    u32 head=r[0], tail=r[1];
    u32 qlen=(tail>=head)?(tail-head):(cap-head+tail);
    if(qlen>=cap) return;                               /* full: drop */
    u32 slot=tail%cap;
    volatile u32* ev=(volatile u32*)(g_mmio_base+0xC000+12+slot*16);
    ev[0]=(u32)kind; ev[1]=(u32)x; ev[2]=(u32)y; ev[3]=(u32)btn;
    r[1]=(tail+1)%cap;
}

/* scancode set 1 -> RayOS key codes (ASCII + 8/9/10/27, arrows 128..131) */
static const u8 kmap[0x40] = {
 [0x01]=27,[0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',[0x07]='6',[0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',
 [0x0C]='-',[0x0D]='=',[0x0E]=8,[0x0F]=9,
 [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',[0x15]='y',[0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',[0x1A]='[',[0x1B]=']',[0x1C]=10,
 [0x1E]='a',[0x1F]='s',[0x20]='d',[0x21]='f',[0x22]='g',[0x23]='h',[0x24]='j',[0x25]='k',[0x26]='l',[0x27]=';',[0x28]='\'',[0x29]='`',
 [0x2B]='\\',[0x2C]='z',[0x2D]='x',[0x2E]='c',[0x2F]='v',[0x30]='b',[0x31]='n',[0x32]='m',[0x33]=',',[0x34]='.',[0x35]='/',
 [0x37]='*',[0x39]=' ',
};
static const u8 kmap_sh[0x40] = {
 [0x01]=27,[0x02]='!',[0x03]='@',[0x04]='#',[0x05]='$',[0x06]='%',[0x07]='^',[0x08]='&',[0x09]='*',[0x0A]='(',[0x0B]=')',
 [0x0C]='_',[0x0D]='+',[0x0E]=8,[0x0F]=9,
 [0x10]='Q',[0x11]='W',[0x12]='E',[0x13]='R',[0x14]='T',[0x15]='Y',[0x16]='U',[0x17]='I',[0x18]='O',[0x19]='P',[0x1A]='{',[0x1B]='}',[0x1C]=10,
 [0x1E]='A',[0x1F]='S',[0x20]='D',[0x21]='F',[0x22]='G',[0x23]='H',[0x24]='J',[0x25]='K',[0x26]='L',[0x27]=':',[0x28]='"',[0x29]='~',
 [0x2B]='|',[0x2C]='Z',[0x2D]='X',[0x2E]='C',[0x2F]='V',[0x30]='B',[0x31]='N',[0x32]='M',[0x33]='<',[0x34]='>',[0x35]='?',
 [0x37]='*',[0x39]=' ',
};

static int g_shift=0, g_ext=0, g_ctrl=0, g_alt=0;
void handle_timer(void){ g_ticks++; outb(0x20,0x20); }
void handle_keyboard(void){
    u8 sc=inb(0x60);
    if(sc==0xE0){ g_ext=1; outb(0x20,0x20); return; }
    int release=sc&0x80; u8 code=sc&0x7F;
    if(g_ext){
        g_ext=0;
        if(code==0x1D){ g_ctrl=release?0:1; outb(0x20,0x20); return; }  /* RCtrl */
        if(code==0x38){ g_alt =release?0:1; outb(0x20,0x20); return; }  /* RAlt  */
        if(code==0x53 && g_ctrl && g_alt){ acpi_reboot(); }            /* Ctrl+Alt+Del */
        int kc=0;
        if(code==0x4B)kc=128; else if(code==0x4D)kc=129;
        else if(code==0x48)kc=130; else if(code==0x50)kc=131;
        if(kc && !release) push_input(2,kc,0,1);
        outb(0x20,0x20); return;
    }
    if(code==0x1D){ g_ctrl=release?0:1; outb(0x20,0x20); return; }      /* LCtrl */
    if(code==0x38){ g_alt =release?0:1; outb(0x20,0x20); return; }      /* LAlt  */
    if(code==0x2A||code==0x36){ g_shift = release?0:1; outb(0x20,0x20); return; }
    if(!release && code<0x40){
        u8 ch = g_shift ? kmap_sh[code] : kmap[code];
        if(ch) push_input(2,(int)ch,0,1);
    }
    outb(0x20,0x20);
}
static int g_mpk=0; static u8 g_mb[3];
static int g_mx=640,g_my=400,g_mbtn=0;
void handle_mouse(void){
    u8 b=inb(0x60);
    if(g_mpk==0 && !(b&0x08)){ outb(0xA0,0x20); outb(0x20,0x20); return; } /* resync */
    g_mb[g_mpk++]=b;
    if(g_mpk<3){ outb(0xA0,0x20); outb(0x20,0x20); return; }
    g_mpk=0;
    u8 f=g_mb[0];
    int dx=g_mb[1]; if(f&0x10) dx-=256;
    int dy=g_mb[2]; if(f&0x20) dy-=256;
    g_mx+=dx; g_my-=dy;                                  /* mouse Y is inverted */
    if(g_mx<0)g_mx=0; if(g_mx>1279)g_mx=1279;
    if(g_my<0)g_my=0; if(g_my>799)g_my=799;
    push_input(0,g_mx,g_my,g_mbtn);                      /* MOVE */
    cursor_move(g_mx,g_my);                              /* live cursor overlay */
    int nb=(f&1)?1:0;
    if(nb!=g_mbtn){ g_mbtn=nb; push_input(1,g_mx,g_my,g_mbtn); }  /* BUTTON change */
    outb(0xA0,0x20); outb(0x20,0x20);                    /* EOI slave + master */
}

/* ── syscall dispatch (Linux x86-64 numbers) ────────────────────────────── */
/* ── network socket layer (net.c); socket fds are >=100 ─────────────────── */
extern long net_socket(long,long);
extern long net_connect(long,const u8*,u16);
extern long net_sendto(long,const u8*,long,const u8*,u16);
extern long net_recvfrom(long,u8*,long);
extern long net_send(long,const u8*,long);
extern long net_recv(long,u8*,long);
extern long net_close(long);

long do_syscall(long num,long a1,long a2,long a3,long a4,long a5,long a6){
    (void)a3;(void)a6;
    switch(num){
        case 0:                                             /* read(fd,buf,len) */
            if(a1>=100) return net_recv(a1,(u8*)a2,a3);     /* TCP socket */
            return 0;
        case 1: {                                           /* write(fd,buf,len) */
            long fd=a1; const u8*buf=(const u8*)a2; u64 len=(u64)a3;
            if(fd>=100) return net_send(fd,buf,len);        /* TCP socket */
            if(fd==1||fd==2){ iolock(); for(u64 i=0;i<len;i++) sputc((char)buf[i]); iounlock(); return (long)len; }
            if(fd>=0 && fd<64 && fd_type[fd]==2){ blit_ppm(buf,len); return (long)len; }
            return (long)len;                               /* generic sink */
        }
        case 41: return net_socket(a1,a2);                  /* socket(family,type) */
        case 42: { const u8*sa=(const u8*)a2;               /* connect */
            return net_connect(a1, sa+4, (u16)((sa[2]<<8)|sa[3])); }
        case 44: { const u8*sa=(const u8*)a5;               /* sendto (UDP) */
            return net_sendto(a1,(const u8*)a2,a3, sa+4, (u16)((sa[2]<<8)|sa[3])); }
        case 45: return net_recvfrom(a1,(u8*)a2,a3);        /* recvfrom (UDP) */
        case 54: return 0;                                  /* setsockopt (stub) */
        case 2: {                                           /* open(path,flags,mode) */
            const char*path=(const char*)a1; int fd=-1;
            for(int k=3;k<64;k++){ if(fd_type[k]==0){ fd=k; break; } }
            if(fd<0) return -24;                            /* EMFILE */
            if(contains(path,"rayos_mmio")) fd_type[fd]=4;  /* input/MMIO chamber file */
            else if(contains(path,".ppm"))  fd_type[fd]=2;  /* rendered frame -> LFB */
            else                            fd_type[fd]=3;  /* sink */
            return fd;
        }
        case 3:  if(a1>=100) return net_close(a1);          /* close */
                 if(a1>=0 && a1<64) fd_type[a1]=0; return 0;
        case 9: {                                           /* mmap(addr,len,prot,flags,fd,off) */
            long r=sys_mmap((u64)a1,(u64)a2,(u64)a4);
            if(a5>=0 && a5<64 && fd_type[a5]==4 && r>0) g_mmio_base=(u64)r;
            return r;
        }
        case 11: free_heap((u64)a1,(u64)a2); return 0;      /* munmap -> free list */
        case 21: return -2;                                 /* access -> ENOENT */
        case 35: {                                          /* nanosleep(req,rem) via PIT */
            const long*ts=(const long*)a1;
            if(ts){ u64 ms=(u64)ts[0]*1000ULL + (u64)ts[1]/1000000ULL;
                u64 start=g_ticks; __asm__ volatile("sti");
                while(g_ticks-start < ms) __asm__ volatile("hlt");
            }
            return 0;
        }
        case 39: return 1;                                  /* getpid */
        case 77: return 0;                                  /* ftruncate */
        case 82: return 0;                                  /* rename */
        case 201: { long t=1720569600L + (long)(g_ticks/1000ULL); if(a1)*(long*)a1=t; return t; }  /* time */
        case 60: case 231:                                  /* exit / exit_group */
            sputs("\r\n[RayOS] guest exit, code="); sputu((u32)a1);
            sputs(" -> ACPI poweroff\r\n");
            acpi_poweroff();
        default:
            sputs("[RayOS] unhandled syscall "); sputu((u32)num); sputs("\r\n");
            return -38;                                     /* ENOSYS */
    }
}

/* ── ELF64 loader ───────────────────────────────────────────────────────── */
static u64 load_elf(u64 base){
    u8*e=(u8*)base;
    u64 entry=*(u64*)(e+0x18), phoff=*(u64*)(e+0x20);
    u16 phsize=*(u16*)(e+0x36), phnum=*(u16*)(e+0x38);
    for(u16 i=0;i<phnum;i++){
        u8*ph=e+phoff+(u64)i*phsize;
        if(*(u32*)(ph+0)!=1) continue;                      /* PT_LOAD */
        u64 off=*(u64*)(ph+8), va=*(u64*)(ph+0x10);
        u64 filesz=*(u64*)(ph+0x20), memsz=*(u64*)(ph+0x28);
        kmemcpy((u8*)va, e+off, filesz);
        if(memsz>filesz) kmemset((u8*)va+filesz,0,memsz-filesz);
        sputs("[RayOS] seg va="); sputx64(va); sputs(" filesz="); sputu((u32)filesz);
        sputs(" memsz="); sputu((u32)memsz); sputs("\r\n");
    }
    return entry;
}

extern u8 __bss_start[], __bss_end[];

void kmain64(void){
    volatile u32* vi=(volatile u32*)0x5000;
    u32 mb=*(volatile u32*)0x5014;
    sputs("[RayOS] 64-bit kernel up. mbinfo="); sputx64(mb); sputs("\r\n");

    /* clear the whole framebuffer to a dark colour so any area the 1280x800
       desktop doesn't cover (e.g. a larger VESA mode's border) looks clean */
    u8*fb=(u8*)(u64)vi[0]; u32 w=vi[1],h=vi[2],pitch=vi[3];
    for(u32 y=0;y<h;y++) for(u32 x=0;x<w;x++){
        u8*p=fb+(u64)y*pitch+(u64)x*4; p[0]=0x16;p[1]=0x0b;p[2]=0x0a;p[3]=0; }
    u32 flags=*(u32*)(u64)mb;
    if((flags&(1u<<3))==0){ sputs("[RayOS] FATAL: no module.\r\n"); goto halt; }
    u32 mods_count=*(u32*)(u64)(mb+20), mods_addr=*(u32*)(u64)(mb+24);
    if(mods_count<1){ sputs("[RayOS] FATAL: 0 modules.\r\n"); goto halt; }
    u32 mod_start=*(u32*)(u64)mods_addr;
    sputs("[RayOS] module @"); sputx64(mod_start); sputs("\r\n");

    /* Copy the guest ELF segments OUT to 0x400000/0x4000000 BEFORE we touch
       .bss — QEMU loads the module right after our payload, overlapping .bss. */
    u64 entry=load_elf((u64)mod_start);

    for(u8* p=__bss_start; p<__bss_end; p++) *p=0;   /* objcopy drops .bss — zero it ourselves */
    /* center the 1280x800 canvas in the framebuffer (AFTER .bss zero, since the
       offset globals live in .bss) */
    if(w>1280) g_off_x=(int)(w-1280)/2;
    if(h>800)  g_off_y=(int)(h-800)/2;
    setup_tss();          /* IST stack for IRQ/exception handlers */
    build_idt();          /* exception + IRQ gates (IST1) */
    enable_sse();
    pic_remap();          /* 8259: IRQ0/1/2/12 -> vec 0x20/0x21/0x22/0x2C */
    pit_init(1000);       /* 1000 Hz tick for time/nanosleep */
    ps2_mouse_init();     /* enable PS/2 aux + IRQ1/IRQ12 */
    persistence_demo();   /* ATA block driver: prove disk read/write persists */
    setup_syscall();

    sputs("[RayOS] entering guest @"); sputx64(entry); sputs(" (interrupts on)\r\n");
    __asm__ volatile("sti");                 /* enable IRQs: PIT/keyboard/mouse now live */
    net_init_and_test();                     /* bring up virtio-net + ARP the gateway */
    smp_init();                              /* bring up the other CPU cores */
    __asm__ volatile("mov $0x3F0000, %%rsp\n\t xor %%rbp,%%rbp\n\t jmp *%0"::"r"(entry));
halt:
    for(;;) __asm__ volatile("hlt");
}
