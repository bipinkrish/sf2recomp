"""Force the SDK's fast notify into the kernel mutex-release window."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#define _GNU_SOURCE 1
#include <assert.h>
#include <stdint.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <stdlib.h>
static void after_store(void *address);
#define __atomic_store_n(p,v,o) (__atomic_store_n((p),(v),(o)), after_store((void *)(p)))
#include "KERNEL_SOURCE"
#undef __atomic_store_n
static uint32_t mutex_word, key;
static pthread_mutex_t race_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t race_cv=PTHREAD_COND_INITIALIZER;
static int released, checked, armed;
uint64_t plat_now_ns(void) {
    struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000000000ull+ts.tv_nsec;
}
static void after_store(void *address) {
    if(address!=&mutex_word || __atomic_load_n(&mutex_word,__ATOMIC_SEQ_CST)!=0)return;
    pthread_mutex_lock(&race_lock);
    if(armed) {
        armed=0;released=1;pthread_cond_broadcast(&race_cv);
        while(!checked)pthread_cond_wait(&race_cv,&race_lock);
    }
    pthread_mutex_unlock(&race_lock);
}
static void *notify(void *arg) {
    t_cur=arg;
    pthread_mutex_lock(&race_lock);
    while(!released)pthread_cond_wait(&race_cv,&race_lock);
    pthread_mutex_unlock(&race_lock);
    // Native code can acquire the released word and skip the kernel entirely
    // when the condition-variable key still says there are no waiters.
    uint32_t zero=0;
    while(!__atomic_compare_exchange_n(&mutex_word,&zero,0x102,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST)) {
        zero=0;sched_yield();
    }
    unsigned has_waiters=__atomic_load_n(&key,__ATOMIC_SEQ_CST);
    pthread_mutex_lock(&race_lock);checked=1;pthread_cond_broadcast(&race_cv);pthread_mutex_unlock(&race_lock);
    if(has_waiters)svc_signal_process_wide_key((uintptr_t)&key,1);
    svc_arbitrate_unlock((uintptr_t)&mutex_word);
    return NULL;
}
int main(void) {
    unsetenv("SF2RECOMP_DEBUG_CV_LATE_WAIT");ktrace_on=0;
    KThread waiter={.handle=0x101,.id=1},notifier={.handle=0x102,.id=2};
    pthread_cond_init(&waiter.cond,NULL);pthread_cond_init(&notifier.cond,NULL);
    for(int i=0;i<1000;++i) {
        mutex_word=0x101;key=0;released=checked=0;armed=1;t_cur=&waiter;
        pthread_t thread;assert(!pthread_create(&thread,NULL,notify,&notifier));
        uint32_t result=svc_wait_process_wide_key((uintptr_t)&mutex_word,(uintptr_t)&key,0x101,200000000);
        pthread_join(thread,NULL);
        assert(result==R_OK && "notify was lost between mutex release and waiter publication");
        assert(mutex_word==0x101 && key==0 && !g_waitlist);
        mutex_word=0;
    }
    pthread_cond_destroy(&waiter.cond);pthread_cond_destroy(&notifier.cond);
}
'''

class ConditionWaitTests(unittest.TestCase):
    def test_notify_during_mutex_release_is_not_lost(self):
        source=(ROOT/'runtime/kernel.c').read_text()
        with tempfile.TemporaryDirectory(prefix='sf2-condition-race-') as directory:
            work=Path(directory)
            kernel=work/'kernel_under_test.c';kernel.write_text(source)
            harness=work/'test.c';harness.write_text(HARNESS.replace('KERNEL_SOURCE',str(kernel)))
            binary=work/'test'
            command=['clang','-O2','-g','-Wno-format','-ffunction-sections','-fdata-sections',
                     '-I'+str(ROOT/'runtime'),str(harness),'-Wl,--gc-sections','-pthread','-ldl','-o',str(binary)]
            subprocess.run(command,check=True)
            subprocess.run([str(binary)],check=True,timeout=15)
            # Prove that the same forced schedule detects the former ordering.
            published='    __atomic_store_n(u32p(key), 1, __ATOMIC_SEQ_CST);\n'
            self.assertEqual(source.count(published),1)
            old=source.replace(published,'').replace('    mutex_release(mutex);','    mutex_release(mutex);\n'+published,1)
            kernel.write_text(old)
            subprocess.run(command,check=True)
            failed=subprocess.run([str(binary)],capture_output=True,timeout=5)
            self.assertNotEqual(failed.returncode,0,'regression must reproduce the original lost notification')
            self.assertIn(b'notify was lost',failed.stderr)
