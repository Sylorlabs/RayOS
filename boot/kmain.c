/* boot/kmain.c — 32-bit freestanding entry (Milestone 1). QEMU's multiboot
   loader won't hand us a linear framebuffer, so we bring the display up
   ourselves: program the Bochs VBE (dispi) registers on the std-VGA device to
   1280x800x32 and locate the linear framebuffer via PCI BAR0. Then draw a test
   pattern and log to COM1. No libc, no 64-bit math, no Linux, no bootloader FB. */
typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;

/* ── port I/O ───────────────────────────────────────────────────────────── */
static inline void outb(u32 p, u8 v){ __asm__ volatile("outb %0,%w1"::"a"(v),"Nd"(p)); }
static inline u8   inb (u32 p){ u8 r; __asm__ volatile("inb %w1,%0":"=a"(r):"Nd"(p)); return r; }
static inline void outw(u32 p, u16 v){ __asm__ volatile("outw %0,%w1"::"a"(v),"Nd"(p)); }
static inline void outl(u32 p, u32 v){ __asm__ volatile("outl %0,%w1"::"a"(v),"Nd"(p)); }
static inline u32  inl (u32 p){ u32 r; __asm__ volatile("inl %w1,%0":"=a"(r):"Nd"(p)); return r; }

/* ── COM1 serial (headless confirmation) ────────────────────────────────── */
static void serial_init(void){
    outb(0x3F8+1,0); outb(0x3F8+3,0x80); outb(0x3F8+0,3); outb(0x3F8+1,0);
    outb(0x3F8+3,3); outb(0x3F8+2,0xC7); outb(0x3F8+4,0x0B);
}
static void sputc(char c){ while((inb(0x3F8+5)&0x20)==0){} outb(0x3F8,(u8)c); }
static void sputs(const char*s){ while(*s) sputc(*s++); }
static void sputu(u32 v){ char b[16]; int i=0; if(!v){sputc('0');return;}
    while(v){b[i++]='0'+v%10; v/=10;} while(i) sputc(b[--i]); }
static void sputx(u32 v){ sputs("0x"); for(int s=28;s>=0;s-=4){ u8 n=(v>>s)&0xF;
    sputc(n<10?'0'+n:'a'+n-10);} }

/* ── PCI config (mechanism #1) ──────────────────────────────────────────── */
static u32 pci_r32(u32 bus,u32 dev,u32 fn,u32 off){
    outl(0xCF8, 0x80000000u|(bus<<16)|(dev<<11)|(fn<<8)|(off&0xFC));
    return inl(0xCFC);
}
static void pci_w32(u32 bus,u32 dev,u32 fn,u32 off,u32 val){
    outl(0xCF8, 0x80000000u|(bus<<16)|(dev<<11)|(fn<<8)|(off&0xFC));
    outl(0xCFC, val);
}
/* Find a display-class device (class 0x03) on bus 0; return its BAR0 (LFB). */
static u32 find_lfb(void){
    for(u32 dev=0; dev<32; dev++){
        u32 idreg = pci_r32(0,dev,0,0x00);
        if((idreg & 0xFFFF)==0xFFFF) continue;               /* no device */
        u32 cls = pci_r32(0,dev,0,0x08) >> 24;               /* class code */
        if(cls==0x03){
            u32 cmd = pci_r32(0,dev,0,0x04);
            pci_w32(0,dev,0,0x04, cmd | 0x6);                /* mem+bus master */
            u32 bar0 = pci_r32(0,dev,0,0x10);
            sputs("[RayOS] VGA at 00:"); sputu(dev); sputs(".0 id="); sputx(idreg);
            sputs(" BAR0="); sputx(bar0 & 0xFFFFFFF0); sputs("\r\n");
            return bar0 & 0xFFFFFFF0;
        }
    }
    return 0;
}

/* ── Bochs VBE (dispi) — set a linear framebuffer mode ───────────────────── */
#define VBE_IDX 0x01CE
#define VBE_DAT 0x01CF
static void vbe_w(u16 idx,u16 val){ outw(VBE_IDX,idx); outw(VBE_DAT,val); }
static void vbe_set(u16 w,u16 h,u16 bpp){
    vbe_w(4, 0);              /* ENABLE = disabled while we reconfigure */
    vbe_w(1, w);             /* XRES */
    vbe_w(2, h);             /* YRES */
    vbe_w(3, bpp);           /* BPP  */
    vbe_w(4, 0x41);          /* ENABLE = enabled | LFB */
}

static void put_px(u8*fb,u32 pitch,u32 x,u32 y,u32 rgb){
    u8*p=fb+y*pitch+x*4; p[0]=rgb&0xFF; p[1]=(rgb>>8)&0xFF; p[2]=(rgb>>16)&0xFF; p[3]=0;
}

void video_setup(void){
    serial_init();
    sputs("\r\n[RayOS] multiboot entry, 32-bit protected mode.\r\n");

    u32 W=1280,H=800,BPP=32, pitch=W*4;
    u32 lfb = find_lfb();
    if(!lfb){ sputs("[RayOS] FATAL: no PCI display device.\r\n"); return; }
    vbe_set(W,H,BPP);
    sputs("[RayOS] VBE set "); sputu(W); sputs("x"); sputu(H); sputs("x");
    sputu(BPP); sputs(" LFB="); sputx(lfb); sputs("\r\n");

    u8*fb=(u8*)lfb;
    for(u32 y=0;y<H;y++){
        u32 t=(y*255)/H;
        u32 col=(((20+t*60/255)&0xFF)<<16)|(((16+t*20/255)&0xFF)<<8)|((48+t*120/255)&0xFF);
        for(u32 x=0;x<W;x++) put_px(fb,pitch,x,y,col);
    }
    u32 pw=640,ph=200,px0=(W-pw)/2,py0=(H-ph)/2;
    for(u32 y=0;y<ph;y++) for(u32 x=0;x<pw;x++) put_px(fb,pitch,px0+x,py0+y,0x1b2340);
    for(u32 x=0;x<pw;x++){ put_px(fb,pitch,px0+x,py0,0x5cd0ff); put_px(fb,pitch,px0+x,py0+3,0x5cd0ff); }
    for(u32 y=0;y<120;y++) for(u32 x=0;x<120;x++)
        put_px(fb,pitch,px0+40+x,py0+40+y, ((x+y)&16)?0x5cd0ff:0x8a5cff);

    /* publish video info to the shared page for the 64-bit stage */
    volatile u32 *vi = (volatile u32*)0x5000;
    vi[0]=lfb; vi[1]=W; vi[2]=H; vi[3]=pitch; vi[4]=0x52415921; /* "RAY!" */
    sputs("[RayOS] 32-bit framebuffer drawn; entering long mode...\r\n");
}
