/* boot/efi/efi_main.c — UEFI (GOP) boot for RayOS on modern hardware. Built
   gnu-efi-less (gcc+ld+objcopy pei-x86-64), fully PIC/reloc-free so no crt0
   self-relocator is needed. Milestone A: locate GOP, draw to the framebuffer,
   print to the console. Kernel load + handoff is layered on next. */
typedef unsigned char   u8;
typedef unsigned short  u16;
typedef unsigned int    u32;
typedef unsigned long long u64;
typedef u64 UINTN;
typedef u64 EFI_STATUS;
typedef void* EFI_HANDLE;
typedef u16 CHAR16;
#define MS __attribute__((ms_abi))
#define EFIAPI MS

typedef struct { u32 d1; u16 d2,d3; u8 d4[8]; } EFI_GUID;

typedef struct _STOP {
    void* Reset;
    EFI_STATUS (EFIAPI *OutputString)(struct _STOP*, CHAR16*);
    void* rest[8];
} SIMPLE_TEXT_OUTPUT;

/* Graphics Output Protocol */
typedef struct {
    u32 Version, HorizontalResolution, VerticalResolution, PixelFormat;
    u32 PixelInformation[4];
    u32 PixelsPerScanLine;
} GOP_MODE_INFO;
typedef struct {
    u32 MaxMode, Mode;
    GOP_MODE_INFO* Info;
    UINTN SizeOfInfo;
    u64 FrameBufferBase;
    UINTN FrameBufferSize;
} GOP_MODE;
typedef struct _GOP {
    EFI_STATUS (EFIAPI *QueryMode)(struct _GOP*, u32, UINTN*, GOP_MODE_INFO**);
    EFI_STATUS (EFIAPI *SetMode)(struct _GOP*, u32);
    void* Blt;
    GOP_MODE* Mode;
} GOP;

typedef struct {
    char _hdr[24];
    void* p[2];                       /* RaiseTPL, RestoreTPL */
    void* AllocatePages;              /* [2] */
    void* FreePages;
    void* GetMemoryMap;
    void* AllocatePool;
    void* FreePool;
    void* p2[9];                      /* events(6) + protocol install group up to HandleProtocol... */
    /* We only need LocateProtocol; index it precisely below via a padded layout */
} EFI_BOOT_SERVICES_PARTIAL;

/* Full boot-services function-pointer table (order per UEFI spec) */
typedef struct {
    char Hdr[24];
    void *RaiseTPL,*RestoreTPL;
    EFI_STATUS (EFIAPI *AllocatePages)(int,int,UINTN,u64*);
    void *FreePages;
    EFI_STATUS (EFIAPI *GetMemoryMap)(UINTN*,void*,UINTN*,UINTN*,u32*);
    EFI_STATUS (EFIAPI *AllocatePool)(int,UINTN,void**);
    void *FreePool;
    void *CreateEvent,*SetTimer,*WaitForEvent,*SignalEvent,*CloseEvent,*CheckEvent;
    void *InstallProtocolInterface,*ReinstallProtocolInterface,*UninstallProtocolInterface,*HandleProtocol,*Reserved,*RegisterProtocolNotify,*LocateHandle,*LocateDevicePath,*InstallConfigurationTable;
    void *LoadImage,*StartImage,*Exit,*UnloadImage;
    EFI_STATUS (EFIAPI *ExitBootServices)(EFI_HANDLE, UINTN);
    void *GetNextMonotonicCount,*Stall,*SetWatchdogTimer;
    void *ConnectController,*DisconnectController;
    void *OpenProtocol,*CloseProtocol,*OpenProtocolInformation;
    void *ProtocolsPerHandle,*LocateHandleBuffer;
    EFI_STATUS (EFIAPI *LocateProtocol)(EFI_GUID*, void*, void**);
    void *rest[10];
} EFI_BOOT_SERVICES;

typedef struct {
    char Hdr[24];
    CHAR16* FirmwareVendor;
    u32 FirmwareRevision; u32 _pad;
    EFI_HANDLE ConsoleInHandle; void* ConIn;
    EFI_HANDLE ConsoleOutHandle; SIMPLE_TEXT_OUTPUT* ConOut;
    EFI_HANDLE StdErrHandle; void* StdErr;
    void* RuntimeServices;
    EFI_BOOT_SERVICES* BootServices;
} EFI_SYSTEM_TABLE;

static void puts16(EFI_SYSTEM_TABLE* st, CHAR16* s){ st->ConOut->OutputString(st->ConOut, s); }
static void kmemcpy(u8* d, const u8* s, u64 n){ for(u64 i=0;i<n;i++) d[i]=s[i]; }

/* embedded images (blobs.S) */
extern u8 payload_start[], payload_end[], kernel_start[], kernel_end[];

/* build identity-mapped 4GiB page tables (2MiB pages) in the 6-page region pt */
static void build_identity(u64 pt){
    u64* pml4=(u64*)pt; u64* pdpt=(u64*)(pt+0x1000); u64* pd=(u64*)(pt+0x2000);
    for(int i=0;i<512;i++){ pml4[i]=0; pdpt[i]=0; }
    pml4[0]=(pt+0x1000)|3;
    for(int i=0;i<4;i++) pdpt[i]=(pt+0x2000+(u64)i*0x1000)|3;
    for(int i=0;i<2048;i++) pd[i]=((u64)i<<21)|0x83;   /* present|write|PS */
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* st){
    EFI_BOOT_SERVICES* bs = st->BootServices;
    puts16(st, u"\r\n[RayOS] UEFI entry\r\n");

    EFI_GUID gop_guid = {0x9042a9de,0x23dc,0x4a38,{0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
    GOP* gop = 0;
    if(bs->LocateProtocol(&gop_guid, 0, (void**)&gop) || !gop){
        puts16(st, u"[RayOS] GOP not found\r\n"); for(;;) __asm__("hlt"); }
    u64 fb = gop->Mode->FrameBufferBase;
    u32 w  = gop->Mode->Info->HorizontalResolution;
    u32 h  = gop->Mode->Info->VerticalResolution;
    u32 ppl= gop->Mode->Info->PixelsPerScanLine;

    /* copy embedded images to their runtime addresses (conventional RAM) */
    u64 ksize = (u64)(kernel_end - kernel_start);
    kmemcpy((u8*)0x200000, payload_start, (u64)(payload_end - payload_start));
    kmemcpy((u8*)0x800000, kernel_start, ksize);
    puts16(st, u"[RayOS] kernel staged; page tables + ExitBootServices...\r\n");

    /* reserve 6 pages for identity page tables while boot services live */
    u64 pt = 0;
    if(bs->AllocatePages(0/*AnyPages*/, 2/*LoaderData*/, 6, &pt)){
        puts16(st, u"[RayOS] AllocatePages failed\r\n"); for(;;) __asm__("hlt"); }

    /* get memory map + ExitBootServices (retry once if key goes stale) */
    UINTN msz=0, mkey=0, dsz=0; u32 dver=0; void* mmap=0;
    bs->GetMemoryMap(&msz,0,&mkey,&dsz,&dver);
    msz += 4*dsz;
    bs->AllocatePool(2, msz, &mmap);
    for(int tries=0; tries<3; tries++){
        UINTN sz=msz;
        bs->GetMemoryMap(&sz,mmap,&mkey,&dsz,&dver);
        if(!bs->ExitBootServices(ImageHandle, mkey)) break;
    }

    /* ---- past the point of no return: no boot services ---- */
    __asm__ volatile("cli");

    /* publish framebuffer info + synthesize a multiboot info for kmain64 */
    volatile u32* vi=(volatile u32*)0x5000;
    vi[0]=(u32)fb; vi[1]=w; vi[2]=h; vi[3]=ppl*4;
    *(volatile u32*)0x5100=0x8; *(volatile u32*)0x5114=1; *(volatile u32*)0x5118=0x5200;
    *(volatile u32*)0x5200=0x800000; *(volatile u32*)0x5204=(u32)(0x800000+ksize);
    *(volatile u32*)0x5208=0; *(volatile u32*)0x520C=0;
    *(volatile u32*)0x5014=0x5100;

    /* our own identity 4GiB paging, then jump to the payload @0x200000 */
    build_identity(pt);
    __asm__ volatile("mov %0,%%cr3"::"r"(pt):"memory");
    __asm__ volatile("mov $0x3F0000,%%rsp\n\t xor %%rbp,%%rbp\n\t jmp *%0"::"r"(0x200000ULL));
    for(;;) __asm__("hlt");
    return 0;
}
