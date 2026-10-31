#!/usr/bin/env python3
"""Opt-in hardware test of the runtime's actual driver-owned heap allocator.

Requires Vulkan development files, Clang and glslc. Select a driver using
VK_DRIVER_FILES (e.g. /usr/share/vulkan/icd.d/radeon_icd.json). Allocates the
full guest heap, runs a compute read/modify/write at its beginning, middle and
end, checks host visibility after a fence, then repeats after replacing the heap.
No game files or saves are used. Does not enable external-memory host imports.
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

PRELUDE = r'''
#include <vulkan/vulkan.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#define LOG(...) fprintf(stderr, __VA_ARGS__)
#define FATAL(...) do { LOG(__VA_ARGS__); exit(1); } while (0)
#define CHECK(x) do { VkResult r = (x); if (r != VK_SUCCESS) FATAL("%s: %d\n", #x, r); } while (0)
static struct {
    VkPhysicalDevice phys;
    VkDevice dev;
    VkPhysicalDeviceProperties props;
    bool dmabuf, map_placed, unmap_reserve, managed_heap;
} g_vk;
static VkBuffer gmem_buf;
static VkDeviceMemory gmem_mem;
static uint64_t gmem_size;
static bool plat_reset_guest(uint64_t lo, uint64_t hi) {
    return mmap((void *)(uintptr_t)lo, hi-lo, PROT_READ|PROT_WRITE,
                MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0) == (void *)(uintptr_t)lo;
}
static void plat_write_tracking_off(const char *why) { (void)why; }
'''

MAIN = r'''
int main(int argc, char **argv) {
    assert(argc == 2);
    VkApplicationInfo app = {.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion=VK_API_VERSION_1_3};
    VkInstanceCreateInfo ici = {.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo=&app};
    VkInstance inst;
    CHECK(vkCreateInstance(&ici, NULL, &inst));
    uint32_t n=0;
    CHECK(vkEnumeratePhysicalDevices(inst, &n, NULL));
    assert(n);
    VkPhysicalDevice *pd=calloc(n, sizeof *pd);
    CHECK(vkEnumeratePhysicalDevices(inst, &n, pd));
    // Match the runtime: prefer discrete, then integrated hardware; never silently test the CPU renderer.
    int best=-1, score=-1;
    for (uint32_t i=0;i<n;i++) {
        VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(pd[i], &p);
        int s=p.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU?2:
              p.deviceType==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU?1:-1;
        if (s>score) {best=i;score=s;}
    }
    if (best<0) FATAL("No hardware Vulkan GPU exposed\n");
    g_vk.phys=pd[best]; free(pd);
    vkGetPhysicalDeviceProperties(g_vk.phys, &g_vk.props);
    LOG("Testing %s; heap %llu MiB\n", g_vk.props.deviceName, (unsigned long long)(HEAP_MAX>>20));
    uint32_t nq=0; vkGetPhysicalDeviceQueueFamilyProperties(g_vk.phys, &nq, NULL);
    VkQueueFamilyProperties *qp=calloc(nq,sizeof *qp);
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk.phys, &nq, qp);
    uint32_t family=0; while (family<nq && !(qp[family].queueFlags&VK_QUEUE_COMPUTE_BIT)) family++;
    assert(family<nq); free(qp);
    float priority=1;
    VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    const char *ext[]={VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,
        .pQueueCreateInfos=&qi,.enabledExtensionCount=2,.ppEnabledExtensionNames=ext};
    CHECK(vkCreateDevice(g_vk.phys,&di,NULL,&g_vk.dev));
    g_vk.dmabuf=g_vk.managed_heap=true;
    VkQueue queue; vkGetDeviceQueue(g_vk.dev,family,0,&queue);
    assert(mmap((void *)(uintptr_t)HEAP_BASE,HEAP_MAX,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void *)(uintptr_t)HEAP_BASE);
    const uint64_t offsets[]={0,HEAP_MAX/2,HEAP_MAX-4096};
    // Separate windows exercise the same offset addressing used when maxStorageBufferRange < HEAP_MAX.
    const VkDescriptorSetLayoutBinding bindings[]={
        {0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL},
        {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL},
        {2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL}};
    VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=3,.pBindings=bindings};
    VkDescriptorSetLayout layout; CHECK(vkCreateDescriptorSetLayout(g_vk.dev,&li,NULL,&layout));
    VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&layout};
    VkPipelineLayout pl; CHECK(vkCreatePipelineLayout(g_vk.dev,&pli,NULL,&pl));
    FILE *f=fopen(argv[1],"rb"); assert(f); fseek(f,0,SEEK_END); size_t size=ftell(f); rewind(f);
    uint32_t *spv=malloc(size); assert(fread(spv,1,size,f)==size); fclose(f);
    VkShaderModuleCreateInfo sci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=size,.pCode=spv};
    VkShaderModule shader; CHECK(vkCreateShaderModule(g_vk.dev,&sci,NULL,&shader)); free(spv);
    VkComputePipelineCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=pl,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=shader,.pName="main"}};
    VkPipeline pipeline; CHECK(vkCreateComputePipelines(g_vk.dev,VK_NULL_HANDLE,1,&pci,NULL,&pipeline));
    VkDescriptorPoolSize ps={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,3};
    VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&ps};
    VkDescriptorPool dp; CHECK(vkCreateDescriptorPool(g_vk.dev,&dpi,NULL,&dp));
    VkDescriptorSetAllocateInfo dai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=dp,.descriptorSetCount=1,.pSetLayouts=&layout};
    VkDescriptorSet set; CHECK(vkAllocateDescriptorSets(g_vk.dev,&dai,&set));
    VkCommandPoolCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
    VkCommandPool cp; CHECK(vkCreateCommandPool(g_vk.dev,&cpi,NULL,&cp));
    VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=cp,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer cb; CHECK(vkAllocateCommandBuffers(g_vk.dev,&cai,&cb));
    VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence; CHECK(vkCreateFence(g_vk.dev,&fi,NULL,&fence));
    for (unsigned boot=0;boot<2;boot++) {
        assert(guest_heap_dmabuf());
        VkDescriptorBufferInfo bi[3]; VkWriteDescriptorSet writes[3];
        for (int i=0;i<3;i++) {
            volatile uint32_t *p=(volatile uint32_t *)(uintptr_t)(HEAP_BASE+offsets[i]);
            for (int j=0;j<64;j++) { assert(p[j]==0); p[j]=0x12340000u+boot*1024+i*64+j; }
            bi[i]=(VkDescriptorBufferInfo){gmem_buf,offsets[i],256};
            writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,
                .dstBinding=(uint32_t)i,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&bi[i]};
        }
        vkUpdateDescriptorSets(g_vk.dev,3,writes,0,NULL);
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CHECK(vkBeginCommandBuffer(cb,&begin));
        vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
        vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pl,0,1,&set,0,NULL);
        vkCmdDispatch(cb,1,1,1);
        VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
        CHECK(vkEndCommandBuffer(cb));
        VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cb};
        CHECK(vkQueueSubmit(queue,1,&si,fence));
        CHECK(vkWaitForFences(g_vk.dev,1,&fence,VK_TRUE,10000000000ull));
        for (int i=0;i<3;i++) {
            volatile uint32_t *p=(volatile uint32_t *)(uintptr_t)(HEAP_BASE+offsets[i]);
            for (int j=0;j<64;j++) assert(p[j]==((0x12340000u+boot*1024+i*64+j)^0xa5a5a5a5u));
        }
        LOG("PASS boot %u: CPU -> compute shader -> CPU, beginning/middle/end\n",boot+1);
        CHECK(vkResetCommandBuffer(cb,0)); CHECK(vkResetFences(g_vk.dev,1,&fence));
        heap_unmap_placed(); assert(plat_reset_guest(HEAP_BASE,HEAP_BASE+HEAP_MAX));
        vkDestroyBuffer(g_vk.dev,gmem_buf,NULL); vkFreeMemory(g_vk.dev,gmem_mem,NULL);
        gmem_buf=VK_NULL_HANDLE; gmem_mem=VK_NULL_HANDLE; gmem_dmabuf=false; gmem_size=0;
    }
    munmap((void *)(uintptr_t)HEAP_BASE,HEAP_MAX);
    vkDestroyFence(g_vk.dev,fence,NULL); vkDestroyCommandPool(g_vk.dev,cp,NULL);
    vkDestroyDescriptorPool(g_vk.dev,dp,NULL); vkDestroyPipeline(g_vk.dev,pipeline,NULL);
    vkDestroyShaderModule(g_vk.dev,shader,NULL); vkDestroyPipelineLayout(g_vk.dev,pl,NULL);
    vkDestroyDescriptorSetLayout(g_vk.dev,layout,NULL); vkDestroyDevice(g_vk.dev,NULL); vkDestroyInstance(inst,NULL);
}
'''


def main():
    source = (ROOT / "runtime/nvn/vulkan/exec.c").read_text()
    start = source.index("static bool gmem_dmabuf;")
    end = source.index("\n#endif\n\n// The guest heap's GPU memory", start)
    constants = "\n".join(line for line in (ROOT / "runtime/rt.h").read_text().splitlines()
                          if line.startswith(("#define HEAP_BASE ", "#define HEAP_MAX ")))
    with tempfile.TemporaryDirectory(prefix="sf2-gpu-heap-") as directory:
        work = pathlib.Path(directory)
        (work / "test.c").write_text(PRELUDE + constants + "\n" + source[start:end] + MAIN)
        (work / "test.comp").write_text("""#version 450
layout(local_size_x=64) in;
layout(set=0,binding=0,std430) buffer A { uint a[]; };
layout(set=0,binding=1,std430) buffer B { uint b[]; };
layout(set=0,binding=2,std430) buffer C { uint c[]; };
void main() { uint i=gl_LocalInvocationIndex; a[i]^=0xa5a5a5a5u; b[i]^=0xa5a5a5a5u; c[i]^=0xa5a5a5a5u; }
""")
        subprocess.run(["glslc", str(work / "test.comp"), "-o", str(work / "test.spv")], check=True)
        subprocess.run(["clang", "-O2", "-Wall", "-Wextra", str(work / "test.c"),
                        "-lvulkan", "-o", str(work / "test")], check=True)
        subprocess.run([str(work / "test"), str(work / "test.spv")], check=True, timeout=45)


if __name__ == "__main__":
    try:
        main()
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, FileNotFoundError) as error:
        print(f"GPU heap test failed: {error}", file=sys.stderr)
        sys.exit(1)
