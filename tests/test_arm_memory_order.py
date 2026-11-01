"""ARM RCsc store/load ordering must survive translation onto the host."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
HARNESS=r'''
#define _GNU_SOURCE 1
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include "cpu.h"
enum { N=200000 };
static uint32_t x __attribute__((aligned(64))),y __attribute__((aligned(64)));
static uint32_t result[2];
static pthread_barrier_t start,finish;
static void *worker(void *arg) {
    uintptr_t id=(uintptr_t)arg;
    for(unsigned i=0;i<N;++i) {
        pthread_barrier_wait(&start);
        // STLR X; LDAR Y versus STLR Y; LDAR X. RCsc forbids both
        // loads seeing zero; C release/acquire allowed that on x86.
        if(!id) { stl32((uintptr_t)&x,1);result[id]=(uint32_t)lda32((uintptr_t)&y); }
        else { stl32((uintptr_t)&y,1);result[id]=(uint32_t)lda32((uintptr_t)&x); }
        pthread_barrier_wait(&finish);
    }
    return NULL;
}
int main(void) {
    pthread_barrier_init(&start,NULL,3);pthread_barrier_init(&finish,NULL,3);
    pthread_t a,b;assert(!pthread_create(&a,NULL,worker,(void *)0));assert(!pthread_create(&b,NULL,worker,(void *)1));
    unsigned forbidden=0;
    for(unsigned i=0;i<N;++i) {
        __atomic_store_n(&x,0,__ATOMIC_SEQ_CST);__atomic_store_n(&y,0,__ATOMIC_SEQ_CST);
        pthread_barrier_wait(&start);pthread_barrier_wait(&finish);
        if(!result[0] && !result[1])++forbidden;
    }
    pthread_join(a,NULL);pthread_join(b,NULL);
    pthread_barrier_destroy(&start);pthread_barrier_destroy(&finish);
    printf("iterations %u forbidden_both_zero %u\n",N,forbidden);
    assert(!forbidden && "translated ARM RCsc store/load ordering was weakened");
}
'''

class ArmMemoryOrderTests(unittest.TestCase):
    def test_store_release_then_acquire_cannot_form_forbidden_store_buffering_outcome(self):
        with tempfile.TemporaryDirectory(prefix='sf2-arm-rcsc-') as directory:
            work=Path(directory);source=work/'test.c';source.write_text(HARNESS)
            binary=work/'test'
            subprocess.run(['clang','-O2','-I'+str(ROOT/'runtime'),str(source),'-pthread','-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=30)
