/* boot/smp.c — SMP bring-up: enable the Local APIC, discover CPUs from the ACPI
   MADT, and start the Application Processors via INIT-SIPI-SIPI using the
   real-mode trampoline (ap_tramp.S) copied to 0x8000. Each AP enables its LAPIC
   and enters the scheduler idle loop. */
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;

static inline u8 sinb(u32 p){ u8 r; __asm__ volatile("inb %w1,%0":"=a"(r):"Nd"(p)); return r; }
static inline void soutb(u32 p,u8 v){ __asm__ volatile("outb %0,%w1"::"a"(v),"Nd"(p)); }
extern void iolock(void); extern void iounlock(void);
static void plock(void){ iolock(); }
static void punlock(void){ iounlock(); }
static void ss(const char*s){ plock(); while(*s){ while((sinb(0x3F8+5)&0x20)==0){} soutb(0x3F8,(u8)*s++);} punlock(); }
static void su(u32 v){ char b[12]; int i=0; plock(); if(!v){ while((sinb(0x3F8+5)&0x20)==0){} soutb(0x3F8,'0'); punlock(); return;}
    while(v){b[i++]='0'+v%10;v/=10;} while(i){ while((sinb(0x3F8+5)&0x20)==0){} soutb(0x3F8,b[--i]); } punlock(); }

extern volatile u64 g_ticks;
extern u64 net_alloc(u64);
extern u8 ap_tramp_blob[], ap_tramp_blob_end[];

/* ── Local APIC ─────────────────────────────────────────────────────────── */
#define LAPIC 0xFEE00000ULL
static void lapic_w(u32 r,u32 v){ *(volatile u32*)(LAPIC+r)=v; }
static u32  lapic_r(u32 r){ return *(volatile u32*)(LAPIC+r); }
static u32  lapic_id(void){ return lapic_r(0x20)>>24; }
static void lapic_enable(void){ lapic_w(0xF0, lapic_r(0xF0)|0x100); }   /* spurious vec, enable */

static void udelay_ticks(u64 t){ u64 s=g_ticks; while(g_ticks-s < t) __asm__("pause"); }

/* ── ACPI MADT: collect enabled CPU APIC IDs ────────────────────────────── */
static int g_apic_ids[64]; static int g_ncpu=0;
static int sig4(u8* p, const char* s){ return p[0]==s[0]&&p[1]==s[1]&&p[2]==s[2]&&p[3]==s[3]; }

static u8* find_rsdp(void){
    for(u64 a=0xE0000; a<0x100000; a+=16){
        u8* p=(u8*)a;
        if(p[0]=='R'&&p[1]=='S'&&p[2]=='D'&&p[3]==' '&&p[4]=='P'&&p[5]=='T'&&p[6]=='R'&&p[7]==' ') return p;
    }
    return 0;
}
static void parse_madt(u8* madt){
    u32 len=*(u32*)(madt+4);
    u8* p=madt+44; u8* end=madt+len;
    while(p<end){
        u8 type=p[0], l=p[1]; if(l<2) break;
        if(type==0){ u8 apic=p[3]; u32 fl=*(u32*)(p+4); if((fl&1)&&g_ncpu<64) g_apic_ids[g_ncpu++]=apic; }
        p+=l;
    }
}
static void discover_cpus(void){
    u8* rsdp=find_rsdp();
    if(!rsdp){ ss("[smp] no RSDP\r\n"); return; }
    u8 rev=rsdp[15];
    if(rev>=2){
        u64 xsdt=*(u64*)(rsdp+24); u8* x=(u8*)xsdt; u32 len=*(u32*)(x+4);
        int n=(len-36)/8;
        for(int i=0;i<n;i++){ u8* t=(u8*)(*(u64*)(x+36+i*8)); if(sig4(t,"APIC")){ parse_madt(t); return; } }
    } else {
        u32 rsdt=*(u32*)(rsdp+16); u8* x=(u8*)(u64)rsdt; u32 len=*(u32*)(x+4);
        int n=(len-36)/4;
        for(int i=0;i<n;i++){ u8* t=(u8*)(u64)(*(u32*)(x+36+i*4)); if(sig4(t,"APIC")){ parse_madt(t); return; } }
    }
}

/* ── AP entry (called from the trampoline in long mode) ─────────────────── */
extern void ap_run(u32 core);            /* proc.c: launch a worker in its own address space */
static volatile int g_online=1;          /* BSP counts as 1 */
void ap_entry64(void){
    lapic_enable();
    u32 id=lapic_id();
    ss("[smp] CPU "); su(id); ss(" online\r\n");
    __sync_fetch_and_add(&g_online,1);
    ap_run(id);                          /* run an isolated flashz worker process here */
    for(;;) __asm__ volatile("hlt");
}

extern void ap_entry64(void);
static u64 read_cr3_raw(void){ u64 v; __asm__ volatile("mov %%cr3,%0":"=r"(v)); return v; }

void smp_init(void){
    lapic_enable();
    u32 bsp=lapic_id();
    discover_cpus();
    ss("[smp] "); su(g_ncpu); ss(" CPU(s) in MADT; BSP apic="); su(bsp); ss("\r\n");
    /* copy the trampoline to 0x8000 and set the fixed patch page at 0x9000 */
    u64 n=(u64)(ap_tramp_blob_end-ap_tramp_blob);
    for(u64 i=0;i<n;i++) ((u8*)0x8000)[i]=ap_tramp_blob[i];
    *(volatile u64*)0x9000 = read_cr3_raw();       /* shared kernel page tables */
    *(volatile u64*)0x9010 = (u64)&ap_entry64;
    for(int i=0;i<g_ncpu;i++){
        u32 apic=g_apic_ids[i];
        if(apic==bsp) continue;
        u64 stk=net_alloc(16384)+16384;
        *(volatile u64*)0x9008 = stk;              /* per-AP stack */
        int before=g_online;
        /* INIT */
        lapic_w(0x310, apic<<24); lapic_w(0x300, 0x00004500); udelay_ticks(10);
        /* SIPI x2, vector = 0x8000>>12 = 0x08 */
        lapic_w(0x310, apic<<24); lapic_w(0x300, 0x00004608); udelay_ticks(1);
        lapic_w(0x310, apic<<24); lapic_w(0x300, 0x00004608);
        u64 t0=g_ticks; while(g_online==before && g_ticks-t0<200) __asm__("pause");
        if(g_online==before){ ss("[smp] CPU apic="); su(apic); ss(" did not start\r\n"); }
    }
    ss("[smp] "); su(g_online); ss(" CPUs online\r\n");
}
