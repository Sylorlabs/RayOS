/* boot/net.c — bare-metal network stack for the RayOS shim: virtio-net driver
   (legacy PCI I/O) + Ethernet/ARP/IPv4/UDP/TCP behind the BSD socket syscalls
   the flashz net stack uses (socket/connect/sendto/recvfrom/read/write). Static
   config for QEMU user-net (10.0.2.15, gw 10.0.2.2). Poll-driven (no threads):
   the stack is serviced whenever the guest does a blocking socket call. */
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;

/* ── serial (own copy; kmain64's is static) ─────────────────────────────── */
static inline void noutb(u32 p,u8 v){ __asm__ volatile("outb %0,%w1"::"a"(v),"Nd"(p)); }
static inline u8   ninb(u32 p){ u8 r; __asm__ volatile("inb %w1,%0":"=a"(r):"Nd"(p)); return r; }
static inline void noutw(u32 p,u16 v){ __asm__ volatile("outw %0,%w1"::"a"(v),"Nd"(p)); }
static inline u16  ninw(u32 p){ u16 r; __asm__ volatile("inw %w1,%0":"=a"(r):"Nd"(p)); return r; }
static inline void noutl(u32 p,u32 v){ __asm__ volatile("outl %0,%w1"::"a"(v),"Nd"(p)); }
static inline u32  ninl(u32 p){ u32 r; __asm__ volatile("inl %w1,%0":"=a"(r):"Nd"(p)); return r; }
static void ns(const char*s){ while(*s){ while((ninb(0x3F8+5)&0x20)==0){} noutb(0x3F8,(u8)*s++);} }
static void nx2(u8 b){ const char* h="0123456789abcdef"; char t[2]={h[b>>4],h[b&15]};
    while((ninb(0x3F8+5)&0x20)==0){} noutb(0x3F8,t[0]); while((ninb(0x3F8+5)&0x20)==0){} noutb(0x3F8,t[1]); }
static void nu(u32 v){ char b[12]; int i=0; if(!v){ns("0");return;} while(v){b[i++]='0'+v%10;v/=10;}
    while(i){ while((ninb(0x3F8+5)&0x20)==0){} noutb(0x3F8,b[--i]); } }

/* ── PCI config (mechanism #1) ──────────────────────────────────────────── */
static u32 pci_r(u32 b,u32 d,u32 f,u32 o){ noutl(0xCF8,0x80000000u|(b<<16)|(d<<11)|(f<<8)|(o&0xFC)); return ninl(0xCFC); }
static void pci_w(u32 b,u32 d,u32 f,u32 o,u32 v){ noutl(0xCF8,0x80000000u|(b<<16)|(d<<11)|(f<<8)|(o&0xFC)); noutl(0xCFC,v); }

/* ── heap for rings/buffers (grab from kmain64's identity heap) ──────────── */
extern u64 net_alloc(u64 len);       /* provided by kmain64 (alloc_heap wrapper) */

/* ── virtio-net (legacy) ────────────────────────────────────────────────── */
static u32 vio_io;                   /* BAR0 I/O base */
static u8  g_mac[6];
#define VQS 256
struct vring_desc { u64 addr; u32 len; u16 flags; u16 next; };
struct vq {
    struct vring_desc* desc;
    u16* avail;                       /* [flags,idx, ring[VQS], used_event] */
    volatile u16* used;               /* [flags,idx, (id,len) ring...] */
    u16 last_used;
    u16 num;
    u64 buf;                          /* backing buffers region */
};
static struct vq rxq, txq;

static void vq_setup(int idx, struct vq* q){
    noutw(vio_io+0x0E, idx);                         /* queue select */
    u16 num = ninw(vio_io+0x0C);
    if(num==0 || num>VQS) num = num? (num>VQS?VQS:num):VQS;
    q->num = num;
    /* legacy vring: desc[num*16], avail[6+2num], pad to 4096, used[6+8num] */
    u64 desc_sz = (u64)num*16;
    u64 avail_sz = 6 + 2*(u64)num;
    u64 used_off = (desc_sz + avail_sz + 4095) & ~4095ULL;
    u64 total = used_off + 6 + 8*(u64)num;
    u64 base = net_alloc((total+4095)&~4095ULL);
    for(u64 i=0;i<total;i++) ((u8*)base)[i]=0;
    q->desc = (struct vring_desc*)base;
    q->avail = (u16*)(base + desc_sz);
    q->used = (volatile u16*)(base + used_off);
    q->last_used = 0;
    /* buffers: num * 2KB */
    q->buf = net_alloc((u64)num*2048);
    noutl(vio_io+0x08, (u32)(base>>12));              /* queue PFN */
}

static void virtio_net_init(void){
    /* find 1af4:1000 (legacy) or 1af4:1041 (modern id, transitional) */
    int bus=0,dev=-1;
    for(int d=0; d<32; d++){
        u32 id=pci_r(0,d,0,0x00);
        if((id&0xFFFF)!=0x1af4) continue;
        u16 did=(id>>16)&0xFFFF;
        if(did==0x1000 || did==0x1041){ dev=d; break; }
    }
    if(dev<0){ ns("[net] no virtio-net device\r\n"); return; }
    /* enable I/O + bus master */
    u32 cmd=pci_r(bus,dev,0,0x04); pci_w(bus,dev,0,0x04, cmd|0x5);
    u32 bar0=pci_r(bus,dev,0,0x10);
    vio_io = bar0 & ~0x3u;                            /* I/O BAR */
    ns("[net] virtio-net at 00:"); nu(dev); ns(".0 io=0x"); nx2((vio_io>>8)&0xFF); nx2(vio_io&0xFF); ns("\r\n");
    /* reset + ack + driver */
    noutb(vio_io+0x12, 0);
    noutb(vio_io+0x12, 1);                            /* ACKNOWLEDGE */
    noutb(vio_io+0x12, 1|2);                          /* DRIVER */
    u32 feat=ninl(vio_io+0x00);
    noutl(vio_io+0x04, feat & 0x20);                  /* accept only VIRTIO_NET_F_MAC(bit5) */
    /* device MAC at config offset 0x14 (legacy, no MSI-X) */
    for(int i=0;i<6;i++) g_mac[i]=ninb(vio_io+0x14+i);
    ns("[net] MAC "); for(int i=0;i<6;i++){ nx2(g_mac[i]); if(i<5) ns(":"); } ns("\r\n");
    /* queues: 0=rx, 1=tx */
    vq_setup(0,&rxq);
    vq_setup(1,&txq);
    /* post all rx buffers */
    for(u16 i=0;i<rxq.num;i++){
        rxq.desc[i].addr = rxq.buf + (u64)i*2048;
        rxq.desc[i].len = 2048;
        rxq.desc[i].flags = 2;                        /* WRITE (device writes) */
        rxq.desc[i].next = 0;
        rxq.avail[2+i] = i;                           /* avail.ring[i]=i */
    }
    __asm__ volatile("":::"memory");
    rxq.avail[1] = rxq.num;                           /* avail.idx */
    noutb(vio_io+0x12, 1|2|4);                        /* DRIVER_OK */
    noutw(vio_io+0x10, 0);                            /* notify rx queue */
    ns("[net] virtio-net ready\r\n");
}

/* TX one ethernet frame (frame includes dst+src+ethertype+payload) */
static u8 g_txhdr_area[2048];
static u16 g_tx_next=0;
static void eth_tx(const u8* frame, u32 len){
    /* build [virtio_net_hdr(10 zero)][frame] in a tx buffer */
    u16 i = g_tx_next % txq.num; g_tx_next++;
    u8* b = (u8*)(txq.buf + (u64)i*2048);
    for(int k=0;k<10;k++) b[k]=0;                     /* virtio_net_hdr */
    for(u32 k=0;k<len;k++) b[10+k]=frame[k];
    txq.desc[i].addr = txq.buf + (u64)i*2048;
    txq.desc[i].len = 10+len;
    txq.desc[i].flags = 0;
    txq.desc[i].next = 0;
    u16 ai = txq.avail[1];
    txq.avail[2 + (ai % txq.num)] = i;
    __asm__ volatile("":::"memory");
    txq.avail[1] = ai+1;
    __asm__ volatile("":::"memory");
    noutw(vio_io+0x10, 1);                            /* notify tx queue */
    (void)g_txhdr_area;
}

/* poll RX: returns a pointer+len of the next received frame (payload after
   virtio_net_hdr), or 0. Recycles the buffer immediately. */
static u32 eth_rx(u8** out){
    if(rxq.last_used == rxq.used[1]) return 0;        /* used.idx unchanged */
    u16 slot = rxq.last_used % rxq.num;
    u32 id  = ((volatile u32*)(rxq.used+2))[slot*2+0];
    u32 tlen= ((volatile u32*)(rxq.used+2))[slot*2+1];
    u8* b = (u8*)(rxq.buf + (u64)id*2048);
    *out = b+10;                                      /* skip virtio_net_hdr */
    rxq.last_used++;
    /* re-post the buffer */
    u16 ai = rxq.avail[1];
    rxq.avail[2 + (ai % rxq.num)] = (u16)id;
    __asm__ volatile("":::"memory");
    rxq.avail[1] = ai+1;
    noutw(vio_io+0x10, 0);
    return tlen>10 ? tlen-10 : 0;
}

/* ── network config + ARP ───────────────────────────────────────────────── */
static u8 g_ip[4]  = {10,0,2,15};
static u8 g_gw[4]  = {10,0,2,2};
static u8 g_gwmac[6];
static int g_gwmac_ok=0;

static u16 be16(u16 v){ return (v>>8)|(v<<8); }
static void put_be16(u8* p, u16 v){ p[0]=v>>8; p[1]=v; }
static void put_be32(u8* p, u32 v){ p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }

/* busy tick using the shared PIT counter (kmain64) */
extern volatile u64 g_ticks;

static void arp_request(const u8* tip){
    u8 f[42];
    for(int i=0;i<6;i++) f[i]=0xFF;                   /* dst broadcast */
    for(int i=0;i<6;i++) f[6+i]=g_mac[i];
    put_be16(f+12, 0x0806);                           /* ARP */
    put_be16(f+14, 0x0001); put_be16(f+16, 0x0800);
    f[18]=6; f[19]=4; put_be16(f+20, 0x0001);         /* request */
    for(int i=0;i<6;i++) f[22+i]=g_mac[i];
    for(int i=0;i<4;i++) f[28+i]=g_ip[i];
    for(int i=0;i<6;i++) f[32+i]=0;
    for(int i=0;i<4;i++) f[38+i]=tip[i];
    eth_tx(f,42);
}

int net_arp_resolve_gw(void){
    if(g_gwmac_ok) return 1;
    for(int tries=0; tries<20; tries++){
        arp_request(g_gw);
        u64 t0=g_ticks;
        while(g_ticks - t0 < 100){                    /* ~100ms */
            u8* fr;
            u32 n=eth_rx(&fr);
            if(n>=42 && fr[12]==0x08 && fr[13]==0x06 && fr[21]==0x02){  /* ARP reply */
                /* sender IP at 28..31, sender MAC at 22..27 */
                if(fr[28]==g_gw[0]&&fr[29]==g_gw[1]&&fr[30]==g_gw[2]&&fr[31]==g_gw[3]){
                    for(int i=0;i<6;i++) g_gwmac[i]=fr[22+i];
                    g_gwmac_ok=1;
                    ns("[net] gateway MAC "); for(int i=0;i<6;i++){nx2(g_gwmac[i]); if(i<5)ns(":");} ns("\r\n");
                    return 1;
                }
            }
        }
    }
    ns("[net] ARP for gateway timed out\r\n");
    return 0;
}


/* ── IP / UDP / TCP + BSD socket layer ──────────────────────────────────── */
static u16 ip_cksum(const u8* d, int n){
    u32 s=0; for(int i=0;i+1<n;i+=2) s+=(d[i]<<8)|d[i+1];
    if(n&1) s+=d[n-1]<<8;
    while(s>>16) s=(s&0xFFFF)+(s>>16);
    return ~s;
}
static u16 g_ipid=1;
/* send an IPv4 packet (proto, dst, payload) to the gateway MAC */
static void ip_send(u8 proto, const u8* dip, const u8* payload, int plen){
    static u8 f[1600];
    for(int i=0;i<6;i++) f[i]=g_gwmac[i];
    for(int i=0;i<6;i++) f[6+i]=g_mac[i];
    put_be16(f+12, 0x0800);
    u8* ip=f+14;
    ip[0]=0x45; ip[1]=0; put_be16(ip+2, 20+plen);
    put_be16(ip+4, g_ipid++); put_be16(ip+6, 0x4000); ip[8]=64; ip[9]=proto;
    ip[10]=0; ip[11]=0;
    for(int i=0;i<4;i++) ip[12+i]=g_ip[i];
    for(int i=0;i<4;i++) ip[16+i]=dip[i];
    u16 c=ip_cksum(ip,20); ip[10]=c>>8; ip[11]=c;
    for(int i=0;i<plen;i++) ip[20+i]=payload[i];
    eth_tx(f, 14+20+plen);
}
static u16 tcp_udp_cksum(const u8* sip,const u8* dip,u8 proto,const u8* seg,int slen){
    u32 s=0; const u8* a=sip;
    s+=(a[0]<<8)|a[1]; s+=(a[2]<<8)|a[3]; a=dip;
    s+=(a[0]<<8)|a[1]; s+=(a[2]<<8)|a[3];
    s+=proto; s+=slen;
    for(int i=0;i+1<slen;i+=2) s+=(seg[i]<<8)|seg[i+1];
    if(slen&1) s+=seg[slen-1]<<8;
    while(s>>16) s=(s&0xFFFF)+(s>>16);
    return ~s;
}

#define NSOCK 16
struct sock {
    int used, type, state;               /* type 1=TCP 2=UDP; state 0 closed 1 syn 2 est 3 finwait 4 closed-by-peer */
    u16 lport, rport; u8 rip[4];
    u32 snd_nxt, rcv_nxt;
    u8 rx[32768]; int rxh, rxt;          /* TCP recv ring */
    u8 dg[2048]; int dglen;              /* UDP last datagram */
};
static struct sock socks[NSOCK];
static u16 g_lport=49152;

static void tcp_out(struct sock* s, u8 flags, const u8* data, int dlen){
    static u8 seg[1600];
    put_be16(seg+0, s->lport); put_be16(seg+2, s->rport);
    put_be32(seg+4, s->snd_nxt); put_be32(seg+8, s->rcv_nxt);
    seg[12]=0x50; seg[13]=flags; put_be16(seg+14, 8192); seg[16]=0; seg[17]=0; put_be16(seg+18,0);
    for(int i=0;i<dlen;i++) seg[20+i]=data[i];
    u16 c=tcp_udp_cksum(g_ip, s->rip, 6, seg, 20+dlen); seg[16]=c>>8; seg[17]=c;
    ip_send(6, s->rip, seg, 20+dlen);
}

/* process one inbound frame; dispatch ARP/UDP/TCP to sockets */
static void net_dispatch(u8* fr, u32 n){
    if(n<14) return;
    u16 et=(fr[12]<<8)|fr[13];
    if(et==0x0806){                                  /* ARP */
        if(n>=42 && fr[21]==0x01 && fr[38]==g_ip[0]&&fr[39]==g_ip[1]&&fr[40]==g_ip[2]&&fr[41]==g_ip[3]){
            static u8 r[42];
            for(int i=0;i<6;i++){ r[i]=fr[22+i]; r[6+i]=g_mac[i]; }
            put_be16(r+12,0x0806); put_be16(r+14,1); put_be16(r+16,0x0800); r[18]=6; r[19]=4; put_be16(r+20,2);
            for(int i=0;i<6;i++) r[22+i]=g_mac[i];
            for(int i=0;i<4;i++) r[28+i]=g_ip[i];
            for(int i=0;i<6;i++) r[32+i]=fr[22+i];
            for(int i=0;i<4;i++) r[38+i]=fr[28+i];
            eth_tx(r,42);
        }
        return;
    }
    if(et!=0x0800 || n<34) return;                   /* IPv4 */
    u8* ip=fr+14; int ihl=(ip[0]&0xF)*4; u8 proto=ip[9];
    u8* sip=ip+12;
    u8* l4=ip+ihl; int l4len=((ip[2]<<8)|ip[3])-ihl;
    if(proto==17){                                    /* UDP */
        u16 dport=(l4[2]<<8)|l4[3]; int ulen=((l4[4]<<8)|l4[5])-8;
        for(int i=0;i<NSOCK;i++){ struct sock* s=&socks[i];
            if(s->used && s->type==2 && s->lport==dport){
                if(ulen>0 && ulen<=2048){ for(int k=0;k<ulen;k++) s->dg[k]=l4[8+k]; s->dglen=ulen; }
                return;
            }
        }
        return;
    }
    if(proto==6){                                     /* TCP */
        u16 sport=(l4[0]<<8)|l4[1], dport=(l4[2]<<8)|l4[3];
        u32 seq=(l4[4]<<24)|(l4[5]<<16)|(l4[6]<<8)|l4[7];
        u32 ack=(l4[8]<<24)|(l4[9]<<16)|(l4[10]<<8)|l4[11];
        (void)ack; u8 flags=l4[13]; int doff=(l4[12]>>4)*4; int dlen=l4len-doff;
        struct sock* s=0;
        for(int i=0;i<NSOCK;i++){ if(socks[i].used&&socks[i].type==1&&socks[i].lport==dport&&socks[i].rport==sport){ s=&socks[i]; break; } }
        if(!s) return;
        if(s->state==1 && (flags&0x12)==0x12){        /* SYN|ACK */
            s->rcv_nxt=seq+1; s->state=2; tcp_out(s,0x10,0,0); return; /* ACK */
        }
        if(s->state>=2){
            if(dlen>0 && seq==s->rcv_nxt){
                for(int k=0;k<dlen;k++){ int nt=(s->rxt+1)%32768; if(nt==s->rxh) break; s->rx[s->rxt]=l4[doff+k]; s->rxt=nt; }
                s->rcv_nxt += dlen; tcp_out(s,0x10,0,0);      /* ACK the data */
            }
            if(flags&0x01){ s->rcv_nxt+=1; tcp_out(s,0x10,0,0); s->state=4; } /* FIN */
        }
    }
}

void net_poll(void){ u8* fr; u32 n; int guard=4096; while(guard-- && (n=eth_rx(&fr))) net_dispatch(fr,n); }

static int slot_of(long fd){ int i=fd-100; if(i<0||i>=NSOCK||!socks[i].used) return -1; return i; }

long net_socket(long family, long type){
    if(family!=2) return -1;
    for(int i=0;i<NSOCK;i++) if(!socks[i].used){
        struct sock* s=&socks[i]; for(unsigned k=0;k<sizeof(*s);k++) ((u8*)s)[k]=0;
        s->used=1; s->type=(type==1)?1:2; s->lport=g_lport++; if(g_lport==0)g_lport=49152;
        return 100+i;
    }
    return -1;
}
long net_close(long fd){ int i=slot_of(fd); if(i<0) return -1;
    if(socks[i].type==1 && socks[i].state==2){ tcp_out(&socks[i],0x11,0,0); socks[i].snd_nxt++; }
    socks[i].used=0; return 0; }

/* UDP */
long net_sendto(long fd, const u8* buf, long len, const u8* dip, u16 dport){
    int i=slot_of(fd); if(i<0||socks[i].type!=2) return -1;
    struct sock* s=&socks[i]; for(int k=0;k<4;k++) s->rip[k]=dip[k]; s->rport=dport;
    static u8 u[2048]; put_be16(u+0,s->lport); put_be16(u+2,dport); put_be16(u+4,8+len); u[6]=0;u[7]=0;
    for(long k=0;k<len;k++) u[8+k]=buf[k];
    u16 c=tcp_udp_cksum(g_ip,dip,17,u,8+len); if(c==0)c=0xFFFF; u[6]=c>>8;u[7]=c;
    ip_send(17, dip, u, 8+len); return len;
}
long net_recvfrom(long fd, u8* buf, long cap){
    int i=slot_of(fd); if(i<0||socks[i].type!=2) return -1;
    struct sock* s=&socks[i];
    u64 t0=g_ticks;
    while(g_ticks-t0 < 3000){ net_poll(); if(s->dglen>0){ int n=s->dglen; if(n>cap)n=cap;
        for(int k=0;k<n;k++) buf[k]=s->dg[k]; s->dglen=0; return n; } }
    return -1;
}
/* TCP */
long net_connect(long fd, const u8* dip, u16 dport){
    int i=slot_of(fd); if(i<0||socks[i].type!=1) return -1;
    struct sock* s=&socks[i];
    for(int k=0;k<4;k++) s->rip[k]=dip[k]; s->rport=dport;
    s->snd_nxt=0x1000+(g_ticks&0xFFFF)*7; s->rcv_nxt=0; s->state=1;
    for(int tries=0;tries<6;tries++){
        tcp_out(s,0x02,0,0);                          /* SYN */
        u32 base=s->snd_nxt; s->snd_nxt=base+1;
        u64 t0=g_ticks; while(g_ticks-t0<500){ net_poll(); if(s->state>=2) return 0; }
        s->snd_nxt=base;                              /* retransmit SYN with same seq */
    }
    return -1;
}
long net_send(long fd, const u8* buf, long len){
    int i=slot_of(fd); if(i<0||socks[i].type!=1||socks[i].state<2) return -1;
    struct sock* s=&socks[i]; long sent=0;
    while(sent<len){ int chunk=len-sent; if(chunk>1400) chunk=1400;
        tcp_out(s,0x18,buf+sent,chunk); s->snd_nxt+=chunk; sent+=chunk;
        u64 t0=g_ticks; while(g_ticks-t0<5) net_poll();  /* let ACKs flow */
    }
    return len;
}
long net_recv(long fd, u8* buf, long cap){
    int i=slot_of(fd); if(i<0||socks[i].type!=1) return -1;
    struct sock* s=&socks[i];
    u64 t0=g_ticks;
    for(;;){
        net_poll();
        if(s->rxh!=s->rxt){ long n=0; while(n<cap && s->rxh!=s->rxt){ buf[n++]=s->rx[s->rxh]; s->rxh=(s->rxh+1)%32768; } return n; }
        if(s->state==4) return 0;                     /* peer closed, no data */
        if(g_ticks-t0>=4000) return 0;                /* timeout */
    }
}


/* build a DNS A-query for host into q; return length */
static int dns_build(u8* q, const char* host){
    q[0]=0x12; q[1]=0x34; q[2]=0x01; q[3]=0x00; q[4]=0;q[5]=1; q[6]=0;q[7]=0; q[8]=0;q[9]=0; q[10]=0;q[11]=0;
    int p=12; int lblstart=p; q[p++]=0;
    for(const char* c=host; ; c++){
        if(*c=='.' || *c==0){ q[lblstart]=(u8)(p-lblstart-1); lblstart=p; q[p++]=0; if(*c==0) break; }
        else q[p++]=*c;
    }
    q[lblstart]=0;                 /* root label */
    q[p++]=0;q[p++]=1;             /* QTYPE A */
    q[p++]=0;q[p++]=1;             /* QCLASS IN */
    return p;
}
/* parse first A record (type=1,rdlen=4) from a DNS response into ip[4] */
static int dns_parse_a(const u8* r, int n, u8* ip){
    if(n<12) return 0; int an=(r[6]<<8)|r[7]; int p=12;
    while(p<n && r[p]) { if((r[p]&0xC0)==0xC0){p+=2; goto q;} p+=r[p]+1; } p++;
q:  p+=4;                          /* skip QTYPE+QCLASS of the question */
    for(int a=0;a<an && p+12<=n;a++){
        if((r[p]&0xC0)==0xC0) p+=2; else { while(p<n && r[p]) p+=r[p]+1; p++; }
        int type=(r[p]<<8)|r[p+1]; int rdl=(r[p+8]<<8)|r[p+9]; p+=10;
        if(type==1 && rdl==4){ ip[0]=r[p];ip[1]=r[p+1];ip[2]=r[p+2];ip[3]=r[p+3]; return 1; }
        p+=rdl;
    }
    return 0;
}
static int nstrlen(const char* s){ int n=0; while(s[n])n++; return n; }

void net_selftest_http(void){
    if(!g_gwmac_ok) return;
    /* DNS: resolve example.com via 1.1.1.1 */
    u8 dns[4]={1,1,1,1}; u8 q[512]; int qn=dns_build(q,"example.com");
    long us=net_socket(2,2);
    net_sendto(us,q,qn,dns,53);
    u8 rb[512]; long rn=net_recvfrom(us,rb,512); net_close(us);
    if(rn<=0){ ns("[net] DNS: no response (host may lack internet)\r\n"); return; }
    u8 ip[4];
    if(!dns_parse_a(rb,(int)rn,ip)){ ns("[net] DNS: no A record\r\n"); return; }
    ns("[net] DNS example.com -> "); nu(ip[0]);ns(".");nu(ip[1]);ns(".");nu(ip[2]);ns(".");nu(ip[3]); ns("\r\n");
    /* TCP: GET / from that IP:80 */
    long ts=net_socket(2,1);
    if(net_connect(ts,ip,80)!=0){ ns("[net] TCP connect failed\r\n"); net_close(ts); return; }
    ns("[net] TCP connected; sending GET\r\n");
    const char* req="GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    net_send(ts,(const u8*)req,nstrlen(req));
    u8 resp[128]; long n=net_recv(ts,resp,120); net_close(ts);
    if(n>0){ resp[n]=0; ns("[net] HTTP reply: ");
        for(long i=0;i<n && resp[i]!='\r' && resp[i]!='\n';i++){ char c=resp[i];
            while((ninb(0x3F8+5)&0x20)==0){} noutb(0x3F8,c); }
        ns("\r\n[net] *** NETWORKING WORKS BARE-METAL ***\r\n");
    } else ns("[net] TCP: no data\r\n");
}
/* self-test called from kmain64 */
void net_init_and_test(void){
    virtio_net_init();
    if(!vio_io) return;
    net_arp_resolve_gw();
}
