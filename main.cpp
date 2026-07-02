#include "filesystem/file_access.h"
#include "filesystem/devfs/devfs.h"
#include "filesystem/fat32/fat32.h"
#include "filesystem/ioctl.h"
#include "filesystem/stringpart.h"
#include "miosix.h"
#include "tusb.h"
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

using namespace std;
using namespace miosix;

namespace usb {

using id = Gpio<PA, 10>;
using dm = Gpio<PA, 11>;
using dp = Gpio<PA, 12>;
using vbus_en = Gpio<PC, 0>;

}

static const bool USB_VERBOSE = false;
static const long long USB_XFER_TIMEOUT_NS = 2000000000LL;
static const uint32_t USB_MAX_SECTORS_PER_XFER = 8;

void *usbThread(void *unused)
{
    iprintf("USB host thread running\n");
    while(!Thread::testTerminate()) tuh_task();
    return nullptr;
}

void OTG_FS_IRQHandler()
{
    tuh_int_handler(0);
}

void OTG_HS_IRQHandler()
{
    tuh_int_handler(1);
}

class USBDevice : public Device
{
public:
    USBDevice(uint8_t devAddr, uint8_t lun)
        : Device(Device::BLOCK), devAddr(devAddr), lun(lun),
          valid(true), xferOk(false) {}

    virtual ssize_t readBlock(void *buffer, size_t size, off_t where);
    virtual ssize_t writeBlock(const void *buffer, size_t size, off_t where);
    virtual int ioctl(int cmd, void *arg);

    void invalidate() { valid = false; }

private:
    static bool onXferComplete(uint8_t dev_addr,
                               tuh_msc_complete_data_t const* cb_data);
    bool awaitCompletion(const char *op);

    uint8_t devAddr;
    uint8_t lun;
    volatile bool valid;
    volatile bool xferOk;
    KernelMutex mutex;
    Semaphore completion;
    uint8_t bounce[USB_MAX_SECTORS_PER_XFER * 512] __attribute__((aligned(4)));
};

bool USBDevice::awaitCompletion(const char *op)
{
    if(completion.timedWait(getTime() + USB_XFER_TIMEOUT_NS)
            == TimedWaitResult::Timeout)
    {
        iprintf("[USB] %s: timeout, marking device gone\n", op);
        valid = false;
        return false;
    }
    if(!xferOk)
    {
        iprintf("[USB] %s: transfer failed\n", op);
        valid = false;
        return false;
    }
    return true;
}

ssize_t USBDevice::readBlock(void *buffer, size_t size, off_t where)
{
    if(where % 512 || size % 512) return -EFAULT;
    uint32_t lba  = static_cast<uint32_t>(where / 512);
    uint32_t nsec = static_cast<uint32_t>(size / 512);
    if(USB_VERBOSE)
        iprintf("[USB] readBlock lba=%lu nsec=%lu\n",
                (unsigned long)lba, (unsigned long)nsec);

    Lock<KernelMutex> l(mutex);
    if(!valid || !tuh_msc_mounted(devAddr))
    {
        iprintf("[USB] readBlock: device not available\n");
        return -ENODEV;
    }

    uint8_t *out = static_cast<uint8_t*>(buffer);
    for(uint32_t done = 0; done < nsec; )
    {
        uint32_t chunk = nsec - done;
        if(chunk > USB_MAX_SECTORS_PER_XFER) chunk = USB_MAX_SECTORS_PER_XFER;

        xferOk = false;
        if(!tuh_msc_read10(devAddr, lun, bounce, lba + done, (uint16_t)chunk,
                           &USBDevice::onXferComplete,
                           reinterpret_cast<uintptr_t>(this)))
        {
            iprintf("[USB] readBlock: could not queue read10\n");
            return -EIO;
        }
        if(!awaitCompletion("readBlock")) return -EIO;
        memcpy(out + done * 512, bounce, chunk * 512);
        done += chunk;
    }
    return size;
}

ssize_t USBDevice::writeBlock(const void *buffer, size_t size, off_t where)
{
    if(where % 512 || size % 512) return -EFAULT;
    uint32_t lba  = static_cast<uint32_t>(where / 512);
    uint32_t nsec = static_cast<uint32_t>(size / 512);
    if(USB_VERBOSE)
        iprintf("[USB] writeBlock lba=%lu nsec=%lu\n",
                (unsigned long)lba, (unsigned long)nsec);

    Lock<KernelMutex> l(mutex);
    if(!valid || !tuh_msc_mounted(devAddr))
    {
        iprintf("[USB] writeBlock: device not available\n");
        return -ENODEV;
    }

    const uint8_t *in = static_cast<const uint8_t*>(buffer);
    for(uint32_t done = 0; done < nsec; )
    {
        uint32_t chunk = nsec - done;
        if(chunk > USB_MAX_SECTORS_PER_XFER) chunk = USB_MAX_SECTORS_PER_XFER;

        memcpy(bounce, in + done * 512, chunk * 512);
        xferOk = false;
        if(!tuh_msc_write10(devAddr, lun, bounce, lba + done, (uint16_t)chunk,
                            &USBDevice::onXferComplete,
                            reinterpret_cast<uintptr_t>(this)))
        {
            iprintf("[USB] writeBlock: could not queue write10\n");
            return -EIO;
        }
        if(!awaitCompletion("writeBlock")) return -EIO;
        done += chunk;
    }
    return size;
}

int USBDevice::ioctl(int cmd, void *arg)
{
    (void) arg;
    if(cmd != IOCTL_SYNC) return -ENOTTY;
    if(!valid || !tuh_msc_mounted(devAddr)) return -EFAULT;
    return 0;
}

struct UsbSlot
{
    intrusive_ref_ptr<USBDevice> dev;
    bool mounted;
};

static UsbSlot g_slots[CFG_TUH_DEVICE_MAX + 1];
static volatile bool g_mountPending[CFG_TUH_DEVICE_MAX + 1];
static volatile bool g_unmountPending[CFG_TUH_DEVICE_MAX + 1];

static void slotPaths(uint8_t dev_addr, char *devName, size_t dn,
                      char *mountPath, size_t mp)
{
    sniprintf(devName, dn, "usbsd%u", dev_addr);
    sniprintf(mountPath, mp, "/usb%u", dev_addr);
}

bool USBDevice::onXferComplete(uint8_t dev_addr,
                               tuh_msc_complete_data_t const* cb_data)
{
    if(dev_addr > CFG_TUH_DEVICE_MAX) return true;
    intrusive_ref_ptr<USBDevice> ref;
    {
        PauseKernelLock pk;
        ref = g_slots[dev_addr].dev;
    }
    if(ref.get() != reinterpret_cast<USBDevice*>(cb_data->user_arg)) return true;
    ref->xferOk = (cb_data->csw &&
                   cb_data->csw->status == MSC_CSW_STATUS_PASSED);
    ref->completion.signal();
    return true;
}

static void listUsb(const char *mountPath)
{
    DIR *d = opendir(mountPath);
    if(d == nullptr)
    {
        iprintf("[USB] opendir(%s) failed\n", mountPath);
        return;
    }
    iprintf("[USB] contents of %s:\n", mountPath);
    struct dirent *de;
    while((de = readdir(d)) != nullptr)
        iprintf("  %s\n", de->d_name);
    closedir(d);
}

static void usbWriteTest(const char *mountPath)
{
    char path[40];
    sniprintf(path, sizeof(path), "%s/MIOSIX.TXT", mountPath);
    const char *msg = "Hello from the Miosix USB driver!\n";

    iprintf("[USB] write test: creating %s\n", path);
    FILE *f = fopen(path, "w");
    if(f == nullptr) { iprintf("[USB] fopen(w) failed errno=%d\n", errno); return; }
    size_t n = fwrite(msg, 1, strlen(msg), f);
    int wrc = fclose(f);
    iprintf("[USB] fwrite=%u fclose=%d\n", (unsigned)n, wrc);

    f = fopen(path, "r");
    if(f == nullptr) { iprintf("[USB] fopen(r) failed errno=%d\n", errno); return; }
    char buf[64] = {0};
    size_t r = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    iprintf("[USB] read back %u bytes: %s", (unsigned)r, buf);
    iprintf("[USB] write test %s\n",
            (r == strlen(msg) && strcmp(buf, msg) == 0) ? "PASSED" : "FAILED");
}

static void usbBigFileTest(const char *mountPath)
{
    char path[40];
    sniprintf(path, sizeof(path), "%s/BIG.BIN", mountPath);
    const uint32_t totalBytes = 256u * 1024u;
    static uint8_t buf[4096] __attribute__((aligned(4)));

    iprintf("[USB] big-file test: writing %lu bytes to %s\n",
            (unsigned long)totalBytes, path);
    FILE *f = fopen(path, "w");
    if(f == nullptr) { iprintf("[USB] fopen(w) failed errno=%d\n", errno); return; }

    long long t0 = getTime();
    uint32_t written = 0;
    while(written < totalBytes)
    {
        uint32_t chunk = totalBytes - written;
        if(chunk > sizeof(buf)) chunk = sizeof(buf);
        for(uint32_t i = 0; i < chunk; i++)
            buf[i] = (uint8_t)((written + i) * 31 + 7);
        size_t n = fwrite(buf, 1, chunk, f);
        if(n != chunk)
        {
            iprintf("[USB] fwrite short %u/%lu errno=%d\n",
                    (unsigned)n, (unsigned long)chunk, errno);
            fclose(f);
            return;
        }
        written += chunk;
    }
    int wrc = fclose(f);
    long long wms = (getTime() - t0) / 1000000LL;
    iprintf("[USB] wrote %lu bytes in %lld ms (%lld KB/s) fclose=%d\n",
            (unsigned long)written, wms,
            wms > 0 ? (long long)written * 1000 / 1024 / wms : 0, wrc);

    f = fopen(path, "r");
    if(f == nullptr) { iprintf("[USB] fopen(r) failed errno=%d\n", errno); return; }
    long long t1 = getTime();
    uint32_t readBytes = 0;
    bool ok = true;
    while(readBytes < totalBytes)
    {
        uint32_t chunk = totalBytes - readBytes;
        if(chunk > sizeof(buf)) chunk = sizeof(buf);
        size_t n = fread(buf, 1, chunk, f);
        if(n != chunk)
        {
            iprintf("[USB] fread short %u/%lu errno=%d\n",
                    (unsigned)n, (unsigned long)chunk, errno);
            ok = false;
            break;
        }
        for(uint32_t i = 0; i < chunk; i++)
            if(buf[i] != (uint8_t)((readBytes + i) * 31 + 7)) { ok = false; break; }
        if(!ok) break;
        readBytes += chunk;
    }
    fclose(f);
    long long rms = (getTime() - t1) / 1000000LL;
    iprintf("[USB] read %lu bytes in %lld ms (%lld KB/s)\n",
            (unsigned long)readBytes, rms,
            rms > 0 ? (long long)readBytes * 1000 / 1024 / rms : 0);
    iprintf("[USB] big-file test %s\n",
            (ok && readBytes == totalBytes) ? "PASSED" : "FAILED");
}

static void usbYankTest(const char *mountPath)
{
    char path[40];
    sniprintf(path, sizeof(path), "%s/STRESS.BIN", mountPath);
    const uint32_t totalBytes = 4u * 1024u * 1024u;
    static uint8_t buf[4096] __attribute__((aligned(4)));

    iprintf("[USB] yank test: writing %lu KB to %s -- pull the stick mid-write!\n",
            (unsigned long)(totalBytes / 1024), path);
    FILE *f = fopen(path, "w");
    if(f == nullptr) { iprintf("[USB] yank test: fopen failed errno=%d\n", errno); return; }

    long long t0 = getTime();
    uint32_t written = 0;
    while(written < totalBytes)
    {
        for(uint32_t i = 0; i < sizeof(buf); i++)
            buf[i] = (uint8_t)((written + i) * 31 + 7);
        if(fwrite(buf, 1, sizeof(buf), f) != sizeof(buf))
        {
            iprintf("[USB] yank test: fwrite FAILED at %lu KB errno=%d (expected on yank)\n",
                    (unsigned long)(written / 1024), errno);
            fclose(f);
            return;
        }
        written += sizeof(buf);
        if(written % (512u * 1024u) == 0)
            iprintf("[USB] yank test: %lu KB written...\n",
                    (unsigned long)(written / 1024));
    }
    int wrc = fclose(f);
    long long ms = (getTime() - t0) / 1000000LL;
    iprintf("[USB] yank test: completed %lu KB in %lld ms fclose=%d (no yank this run)\n",
            (unsigned long)(totalBytes / 1024), ms, wrc);
}

static void doUsbUnmount(uint8_t dev_addr);

static bool doUsbMount(uint8_t dev_addr)
{
    doUsbUnmount(dev_addr);
    if(!tuh_msc_mounted(dev_addr)) return false;

    const uint8_t lun = 0;
    char devName[16];
    char mountPath[16];
    slotPaths(dev_addr, devName, sizeof(devName), mountPath, sizeof(mountPath));

    uint32_t blockCount = tuh_msc_get_block_count(dev_addr, lun);
    uint32_t blockSize  = tuh_msc_get_block_size(dev_addr, lun);
    iprintf("[USB] mounting addr=%u (%s -> %s): %lu blocks x %lu bytes\n",
            dev_addr, devName, mountPath,
            (unsigned long)blockCount, (unsigned long)blockSize);

    auto& fsm = FilesystemManager::instance();
    auto devfs = fsm.getDevFs();
    if(!devfs) { iprintf("[USB] no DevFs\n"); return false; }

    devfs->remove(devName);
    intrusive_ref_ptr<USBDevice> dev(new USBDevice(dev_addr, lun));
    g_slots[dev_addr].dev = dev;
    if(!devfs->addDevice(devName, dev))
    {
        iprintf("[USB] addDevice(%s) failed\n", devName);
        g_slots[dev_addr].dev.reset();
        return false;
    }

    intrusive_ref_ptr<FileBase> disk;
    StringPart name(devName);
    if(devfs->open(disk, name, O_RDWR, 0) < 0)
    {
        iprintf("[USB] open(%s) failed\n", devName);
        devfs->remove(devName);
        g_slots[dev_addr].dev.reset();
        return false;
    }

    for(int i = 0; i < 100 && !tuh_msc_ready(dev_addr); i++) Thread::sleep(10);

    intrusive_ref_ptr<Fat32Fs> fs;
    bool ok = false;
    for(int attempt = 1; attempt <= 3 && !ok; attempt++)
    {
        fs = intrusive_ref_ptr<Fat32Fs>(new Fat32Fs(disk));
        ok = !fs->mountFailed();
        if(!ok)
        {
            iprintf("[USB] FAT32 mount attempt %d failed\n", attempt);
            Thread::sleep(100);
        }
    }
    if(!ok)
    {
        iprintf("[USB] FAT32 mount failed\n");
        devfs->remove(devName);
        g_slots[dev_addr].dev.reset();
        return false;
    }

    if(mkdir(mountPath, 0755) != 0)
        iprintf("[USB] mkdir %s nonzero (may exist)\n", mountPath);
    if(fsm.kmount(mountPath, fs) != 0)
    {
        iprintf("[USB] kmount %s failed\n", mountPath);
        devfs->remove(devName);
        rmdir(mountPath);
        g_slots[dev_addr].dev.reset();
        return false;
    }

    g_slots[dev_addr].mounted = true;
    iprintf("[USB] mounted at %s\n", mountPath);
    return true;
}

static void doUsbUnmount(uint8_t dev_addr)
{
    if(!g_slots[dev_addr].dev && !g_slots[dev_addr].mounted) return;

    char devName[16];
    char mountPath[16];
    slotPaths(dev_addr, devName, sizeof(devName), mountPath, sizeof(mountPath));

    auto& fsm = FilesystemManager::instance();
    if(g_slots[dev_addr].dev) g_slots[dev_addr].dev->invalidate();
    int r = fsm.umount(mountPath, true);
    iprintf("[USB] umount %s -> %d\n", mountPath, r);
    rmdir(mountPath);
    auto devfs = fsm.getDevFs();
    if(devfs) devfs->remove(devName);
    g_slots[dev_addr].dev.reset();
    g_slots[dev_addr].mounted = false;
}

extern "C" void tuh_mount_cb(uint8_t dev_addr)
{
    iprintf("[USB] device attached, addr=%u\n", dev_addr);
}

extern "C" void tuh_umount_cb(uint8_t dev_addr)
{
    iprintf("[USB] device detached, addr=%u\n", dev_addr);
}

extern "C" void tuh_msc_mount_cb(uint8_t dev_addr)
{
    if(dev_addr <= CFG_TUH_DEVICE_MAX)
        __atomic_store_n(&g_mountPending[dev_addr], true, __ATOMIC_RELEASE);
}

extern "C" void tuh_msc_umount_cb(uint8_t dev_addr)
{
    if(dev_addr <= CFG_TUH_DEVICE_MAX)
        __atomic_store_n(&g_unmountPending[dev_addr], true, __ATOMIC_RELEASE);
}

int main()
{
    {
        GlobalIrqLock dLock;

        IRQregisterIrq(dLock, OTG_FS_IRQn, OTG_FS_IRQHandler);
        IRQregisterIrq(dLock, OTG_HS_IRQn, OTG_HS_IRQHandler);

        RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
        RCC_SYNC();

        usb::id::alternateFunction(10);
        usb::dm::alternateFunction(10);
        usb::dp::alternateFunction(10);

        usb::id::speed(Speed::_100MHz);
        usb::dm::speed(Speed::_100MHz);
        usb::dp::speed(Speed::_100MHz);

        usb::id::mode(Mode::ALTERNATE);
        usb::dm::mode(Mode::ALTERNATE);
        usb::dp::mode(Mode::ALTERNATE);

        usb::vbus_en::high();
        usb::vbus_en::mode(Mode::OUTPUT);
    }

    Thread::sleep(1000);
    usb::vbus_en::low();
    Thread::sleep(300);

    iprintf("Initializing TinyUSB host stack...\n");
    if(!tusb_init())
    {
        iprintf("USB host initialization error\n");
        return 0;
    }

    USB_OTG_FS->GCCFG |= USB_OTG_GCCFG_NOVBUSSENS;
    USB_OTG_FS->GCCFG &= ~(USB_OTG_GCCFG_VBUSBSEN | USB_OTG_GCCFG_VBUSASEN);
    iprintf("TinyUSB host initialized OK\n");

    Thread::create(usbThread, 2048, DEFAULT_PRIORITY, nullptr, Thread::DETACHED);

    iprintf("Insert a FAT32 USB stick...\n");
    for(;;)
    {
        for(uint8_t a = 1; a <= CFG_TUH_DEVICE_MAX; a++)
        {
            if(__atomic_exchange_n(&g_unmountPending[a], false, __ATOMIC_ACQ_REL))
                doUsbUnmount(a);
            if(__atomic_exchange_n(&g_mountPending[a], false, __ATOMIC_ACQ_REL))
            {
                if(doUsbMount(a))
                {
                    char mountPath[16];
                    sniprintf(mountPath, sizeof(mountPath), "/usb%u", a);
                    listUsb(mountPath);
                    usbWriteTest(mountPath);
                    usbBigFileTest(mountPath);
                    usbYankTest(mountPath);
                }
            }
        }
        Thread::sleep(50);
    }
}
