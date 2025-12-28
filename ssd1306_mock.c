#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/timer.h> // Dùng timer để giả lập bấm nút

#define DRIVER_NAME "ssd1306_mock"
#define DEVICE_NAME "ssd1306_oled"

#define SSD1306_IOC_MAGIC 'k'
#define SSD1306_IOC_CLEAR_SCREEN _IO(SSD1306_IOC_MAGIC, 0)
#define SSD1306_IOC_UPDATE_FRAME _IOW(SSD1306_IOC_MAGIC, 1, unsigned char *)
#define SSD1306_IOC_SIMULATE_BUTTON _IO(SSD1306_IOC_MAGIC, 99) // Lệnh giả lập nút bấm

MODULE_AUTHOR("Lilac - Simulation Mode");
MODULE_LICENSE("GPL");

struct ssd1306_dev {
    struct cdev cdev;
    dev_t dev_num;
    uint8_t *frame_buffer;
};

struct ssd1306_dev *oled_device;
static bool invert_mode = false;
static struct work_struct invert_work;

// --- MOCK HARDWARE FUNCTIONS (Hàm giả) ---
static void Mock_I2C_Write(unsigned char *data, int len) {
    // Thay vì gửi I2C, ta chỉ in log tượng trưng
    // printk(KERN_DEBUG "MOCK_HW: I2C Sending %d bytes...\n", len);
}

static void Mock_GPIO_LED(int state) {
    printk(KERN_INFO "MOCK_HW: LED is now %s\n", state ? "ON" : "OFF");
}

// --- LOGIC WORKQUEUE (Giữ nguyên logic của bạn) ---
static void invert_display_work_handler(struct work_struct *work) {
    invert_mode = !invert_mode;
    
    // Giả lập gửi lệnh I2C
    Mock_I2C_Write(NULL, 2); 

    // Giả lập chớp đèn
    Mock_GPIO_LED(1);
    msleep(100); 
    Mock_GPIO_LED(0);

    printk(KERN_INFO "DRIVER_LOGIC: Workqueue finished. Display Mode: %s\n", invert_mode ? "INVERTED" : "NORMAL");
}

// --- FILE OPERATIONS ---
static int ssd1306_open(struct inode *inode, struct file *file) { return 0; }
static int ssd1306_release(struct inode *inode, struct file *file) { return 0; }

static long ssd1306_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    unsigned char *kbuf;

    switch (cmd) {
        case SSD1306_IOC_UPDATE_FRAME:
            // Test logic nhận dữ liệu ảnh
            kbuf = kmalloc(1024, GFP_KERNEL); // 128*64/8
            if (!kbuf) return -ENOMEM;
            
            if (copy_from_user(kbuf, (unsigned char __user *)arg, 1024)) {
                kfree(kbuf); return -EFAULT;
            }
            
            // Logic thành công -> Giả lập vẽ
            printk(KERN_INFO "DRIVER_LOGIC: Received Frame (1024 bytes). Drawing to Screen...\n");
            Mock_I2C_Write(kbuf, 1024);
            
            kfree(kbuf);
            return 0;

        case SSD1306_IOC_SIMULATE_BUTTON:
            // Đây là cách ta giả lập nút bấm từ User Space
            printk(KERN_INFO "DRIVER_LOGIC: Simulated Button Press Detected!\n");
            schedule_work(&invert_work); // Kích hoạt Workqueue như thật
            return 0;

        default:
            return -ENOTTY;
    }
}

static const struct file_operations ssd1306_fops = {
    .owner = THIS_MODULE,
    .open = ssd1306_open,
    .release = ssd1306_release,
    .unlocked_ioctl = ssd1306_ioctl,
};

// --- INIT & EXIT ---
static int __init ssd1306_mock_init(void)
{
    oled_device = kzalloc(sizeof(*oled_device), GFP_KERNEL);
    if (!oled_device) return -ENOMEM;

    if (alloc_chrdev_region(&oled_device->dev_num, 0, 1, DEVICE_NAME) < 0) {
        kfree(oled_device); return -1;
    }
    
    cdev_init(&oled_device->cdev, &ssd1306_fops);
    if (cdev_add(&oled_device->cdev, oled_device->dev_num, 1) < 0) {
        unregister_chrdev_region(oled_device->dev_num, 1);
        kfree(oled_device); return -1;
    }

    INIT_WORK(&invert_work, invert_display_work_handler);
    
    printk(KERN_INFO "SSD1306 MOCK DRIVER: Loaded. Major=%d. Ready for simulation.\n", MAJOR(oled_device->dev_num));
    return 0;
}

static void __exit ssd1306_mock_exit(void)
{
    cancel_work_sync(&invert_work);
    cdev_del(&oled_device->cdev);
    unregister_chrdev_region(oled_device->dev_num, 1);
    kfree(oled_device);
    printk(KERN_INFO "SSD1306 MOCK DRIVER: Unloaded.\n");
}

module_init(ssd1306_mock_init);
module_exit(ssd1306_mock_exit);