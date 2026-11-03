// Numerical benchmark only: execute extracted original guest RNG functions
// beside the actual LAN hooks. Optional socket peers check the RNG hashes;
// there is no renderer, menu or UI automation.
#include <assert.h>
#include <sys/mman.h>
#include "simd.h"
#include "replay.c"
#include "lan_random.h"
#include "original_rng.inc"
Settings g_settings;
uint32_t rt_hook_features;
volatile int g_fnprof;
static unsigned char generator[32],object[32],unity_state[16],stack[4096];
static Cpu cpu;
static uint64_t menu_state;
static const uint64_t caller=UINT64_C(0x700102c338);
void fnprof_hit(uint64_t a,uint64_t b,uint64_t d) { abort(); }
void rt_udf(Cpu *c,uint64_t pc) { abort(); }
void f_7001b85980(Cpu *c) { c->x[0]=(uintptr_t)unity_state; }
uint64_t renderer_present_count(void) { return 0; }
uint64_t plat_now_ns(void) { return 0; }
void menu_request_quit(void) { abort(); }
bool menu_quit_requested(void) { return false; }
int rt_hook(Cpu *c,uint64_t pc) { int result=controlled_hook(c,pc);return result<0?0:result; }
void guest_call(Cpu *c,uint64_t pc) {
    switch(pc) {
    case 0x70010f1550ull:f_70010f1550(c);break;
    case 0x70010f1ab0ull:f_70010f1ab0(c);break;
    case 0x70010f1b80ull:f_70010f1b80(c);break;
    case 0x70010f1f90ull:f_70010f1f90(c);break;
    default:abort();
    }
}
static void reset(uint32_t value) {
    memset(&cpu,0,sizeof cpu);cpu.sp=(uintptr_t)(stack+sizeof stack);
    memset(streams,0,sizeof streams);stream_count=0;seed=value;
    controlled=selected=true;simulation_depth=1;random_scope=wrapped=0;
    ST32((uintptr_t)generator+0x10,value);ST64((uintptr_t)object+0x10,(uintptr_t)generator);
    rt_hook_features=0;cpu.x[0]=value;f_70018d6fe0(&cpu);
    menu_state=(uint64_t)value^SF2RECOMP_LAN_SELECTION_SALT;
}
static uint32_t seeds[]={0,1,12345,54321,0x7fffffff,0x80000000,0xdeadbeef,0xffffffff};
// 0/1 native/LAN Nekki floats; 2/3 native/LAN Unity floats;
// 4/5 native/LAN Nekki ints; 6/7 native/LAN Unity ints; 8 new menu choices.
static double draw(unsigned kind,unsigned bound) {
    Cpu *c=&cpu;c->x[30]=caller;rt_hook_features=(kind==1 || kind==5)?2:0;
    if(kind<2) { c->x[0]=(uintptr_t)object;f_70010f1550(c);return VSF32(0); }
    if(kind<4) {
        float lo=0,hi=1;memcpy(&c->v[0],&lo,4);memcpy(&c->v[1],&hi,4);
        if(kind==2)f_70018d7080(c);else assert(controlled_hook(c,0x7001237b50ull)==1);
        return VSF32(0);
    }
    if(kind<6) { c->x[0]=(uintptr_t)object;c->x[1]=bound;f_70010f1ab0(c);return (uint32_t)c->x[0]; }
    if(kind==6) { c->x[0]=0;c->x[1]=bound;f_70018d70f0(c);return (uint32_t)c->x[0]; }
    if(kind==7) { c->x[1]=0;c->x[2]=bound;assert(controlled_hook(c,0x7001237bc0ull)==1);return (uint32_t)c->x[0]; }
    return lan_selection_bounded(&menu_state,bound);
}
typedef struct { double x,y,xx,yy,xy;uint64_t n; } Pair;
static void pair_add(Pair *p,double x,double y) { p->x+=x;p->y+=y;p->xx+=x*x;p->yy+=y*y;p->xy+=x*y;++p->n; }
static double correlation(Pair p) { return (p.n*p.xy-p.x*p.y)/sqrt((p.n*p.xx-p.x*p.x)*(p.n*p.yy-p.y*p.y)); }
static const char *names[]={"native_nekki_float","lan_nekki_float","native_unity_float","lan_unity_float","native_nekki_int","lan_nekki_int","native_unity_int","lan_unity_int","new_menu"};
static uint64_t exact(unsigned samples) {
    uint64_t mismatch=0;
    for(unsigned type=0;type<4;++type)for(unsigned s=0;s<8;++s) {
        reset(seeds[s]);
        // Align the original generator to the LAN stream's actual starting
        // state, then compare the original native conversions bit for bit.
        rt_hook_features=2;cpu.x[30]=caller;cpu.x[0]=(uintptr_t)generator;f_7001177d20(&cpu);
        for(unsigned i=0;i<samples;++i) {
            uint32_t values[2];
            for(unsigned lan=0;lan<2;++lan) {
                Cpu *c=&cpu;rt_hook_features=lan?2:0;c->x[30]=caller;c->x[0]=type?(uintptr_t)object:(uintptr_t)generator;
                if(type==0)f_7001177d20(c);
                if(type==1)f_70010f1550(c);
                if(type==2) { c->x[1]=(uint32_t)-20;c->x[2]=10;f_70010f1b80(c); }
                if(type==3) { float p=25,max=100;memcpy(&c->v[0],&p,4);memcpy(&c->v[1],&max,4);f_70010f1f90(c); }
                values[lan]=type==1?(uint32_t)c->v[0]:(uint32_t)c->x[0];
            }
            mismatch+=values[0]!=values[1];
        }
    }
    printf("{\"kind\":\"exact_native_nekki_comparison\",\"cases\":4,\"seeds\":8,\"comparisons\":%u,\"mismatches\":%llu}\n",samples*32,(unsigned long long)mismatch);
    return mismatch;
}
static void distribution(unsigned kind,unsigned samples,unsigned bound) {
    unsigned bins=kind<4?64:bound;uint64_t counts[256]={0},n=0,repeats=0,outside=0,upper=0;
    double sum=0,sum2=0;Pair lag={0};
    for(unsigned s=0;s<8;++s) {
        reset(seeds[s]);double previous=-1;
        for(unsigned i=0;i<samples;++i) {
            double value=draw(kind,bound),x=kind<4?value:(value+.5)/bound;
            // Native Nekki's float-based integer conversion can round to the
            // upper endpoint. Record that inherited behavior separately.
            if((kind==4 || kind==5) && value==bound) { ++upper;continue; }
            if(value<0 || (kind<4?value>1:value>=bound)) { ++outside;continue; }
            unsigned bucket=kind<4?(unsigned)(value*bins):(unsigned)value;if(bucket==bins)--bucket;
            ++counts[bucket];sum+=x;sum2+=x*x;++n;
            if(previous>=0) { pair_add(&lag,previous,x);repeats+=previous==x; }previous=x;
        }
    }
    double expected=(double)n/bins,chi=0,max_z=0;
    for(unsigned b=0;b<bins;++b) { double d=counts[b]-expected;chi+=d*d/expected;double z=fabs(d)/sqrt(expected*(1-1.0/bins));if(z>max_z)max_z=z; }
    printf("{\"kind\":\"distribution\",\"name\":\"%s\",\"bound\":%u,\"n\":%llu,\"mean\":%.10g,\"variance\":%.10g,\"chi2_per_df\":%.8g,\"max_bucket_z\":%.8g,\"lag1_correlation\":%.8g,\"repeat_fraction\":%.8g,\"out_of_range\":%llu,\"native_upper_endpoint_hits\":%llu}\n",names[kind],kind<4?0:bound,(unsigned long long)n,sum/n,sum2/n-(sum/n)*(sum/n),chi/(bins-1),max_z,correlation(lag),(double)repeats/lag.n,(unsigned long long)outside,(unsigned long long)upper);
}
static void joint(unsigned kind,unsigned samples) {
    uint64_t counts[320]={0},n=0,upper=0;Pair pair={0};
    for(unsigned s=0;s<8;++s) {
        reset(seeds[s]);uint32_t old_seed=seeds[s];
        for(unsigned i=0;i<samples;++i) {
            unsigned a,b,c;
            if(kind==9) { a=old_seed%8;b=(uint32_t)(old_seed+2654435761u)%8;c=(uint32_t)(old_seed+2*2654435761u)%5;old_seed+=0x9e3779b9u; }
            else { a=draw(kind,8);b=draw(kind,8);c=draw(kind,5); }
            if(a>=8 || b>=8 || c>=5) {
                assert((kind==4 || kind==5) && a<=8 && b<=8 && c<=5);++upper;continue;
            }
            ++counts[(a*8+b)*5+c];pair_add(&pair,a,b);++n;
        }
    }
    double expected=(double)n/320,chi=0;unsigned missing=0;
    for(unsigned i=0;i<320;++i) { double d=counts[i]-expected;chi+=d*d/expected;missing+=counts[i]==0; }
    printf("{\"kind\":\"joint_choices\",\"name\":\"%s\",\"n\":%llu,\"missing_triples\":%u,\"chi2_per_df\":%.8g,\"fighter_correlation\":%.8g,\"native_upper_endpoint_triples\":%llu}\n",kind==9?"old_correlated_menu":names[kind],(unsigned long long)n,missing,chi/319,correlation(pair),(unsigned long long)upper);
}
static void cross_seed(unsigned samples) {
    Pair native={0},lan={0};
    for(unsigned i=0;i<samples;++i) {
        reset(i*0x9e3779b9u);double x=draw(6,1000000),y=draw(6,1000000);pair_add(&native,x,y);
        reset(i*0x9e3779b9u);cpu.x[30]=caller;cpu.x[1]=0;cpu.x[2]=1000000;assert(controlled_hook(&cpu,0x7001237bc0ull)==1);x=(uint32_t)cpu.x[0];
        cpu.x[30]=0x700102c394ull;cpu.x[1]=0;cpu.x[2]=1000000;assert(controlled_hook(&cpu,0x7001237bc0ull)==1);y=(uint32_t)cpu.x[0];pair_add(&lan,x,y);
    }
    printf("{\"kind\":\"first_draw_across_seeds\",\"n\":%u,\"native_unity_correlation\":%.8g,\"lan_actual_callsite_correlation\":%.8g}\n",samples,correlation(native),correlation(lan));
}
static int synchronized_peer(void) {
    lan_init();assert(lan_enabled());reset(lan_config()->seed);
    uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned tick=0;tick<1024;++tick) {
        const unsigned kinds[]={1,3,5,7};
        for(unsigned i=0;i<4;++i) {
            double value=draw(kinds[i],60);uint64_t bits;memcpy(&bits,&value,sizeof bits);
            hash=(hash^bits)*UINT64_C(1099511628211);
        }
        LanEvent out[LAN_MAX_EVENTS*2];assert(lan_step(tick,hash,NULL,0,out)==0);
    }
    lan_finish(1024,hash);
    printf("{\"steps\":1024,\"rng_outputs\":4096,\"role\":%u,\"hash\":\"%016" PRIx64 "\"}\n",lan_player(),hash);
    return 0;
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IOLBF,0);
    if(argc>1 && !strcmp(argv[1],"--peer"))return synchronized_peer();
    unsigned samples=argc>1?strtoul(argv[1],0,10):100000;
    void *p=mmap((void *)0x7002ca4000ull,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);assert(p!=(void *)-1);
    ST32(0x7002ca46e0ull,ORIGINAL_UNITY_FLOAT_SCALE);
    uint64_t mismatch=exact(samples);
    for(unsigned kind=0;kind<9;++kind)distribution(kind,samples,60);
    for(unsigned kind=4;kind<=9;++kind)joint(kind,samples);
    cross_seed(samples*8);
    return mismatch?1:0;
}
