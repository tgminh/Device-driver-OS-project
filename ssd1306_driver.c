#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/ioctl.h>
// Ngat moi
#include <linux/interrupt.h> // Xu ly ngat
#include <linux/gpio.h>      // Dieu khien GPIO
#include <linux/workqueue.h> // Xu ly Bottom Half
#include <linux/jiffies.h>   // thoi gian he thong
#include <linux/delay.h>     // Ham msleep

// cau hinh nut S2 va den USR3 cho ngat moi
#define BTN_GPIO 72  // but S2 (GPIO2_8)
#define LED_GPIO 56  // den USR3 (GPIO1_24)
#define SSD1306_IOC_MAGIC 'k'   // Arbitrary unique magic number

#define SSD1306_IOC_CLEAR_SCREEN _IO(SSD1306_IOC_MAGIC, 0)
#define SSD1306_IOC_UPDATE_FRAME _IOW(SSD1306_IOC_MAGIC, 1, unsigned char *) // ioctl cho gif

MODULE_AUTHOR("Lilac");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SSD1306 OLED Display I2C Driver");

#define SSD1306_WIDTH       128
#define SSD1306_HEIGHT      64
#define SSD1306_PAGES       SSD1306_HEIGHT/8      // 64-pixel high display (64 / 8 bits = 8 pages)
#define FRAME_BUFFER_SIZE   (SSD1306_WIDTH * SSD1306_PAGES)
#define DRIVER_NAME         "ssd1306_oled"
#define DEVICE_NAME         "ssd1306_oled" 

#define COMMAND_MODE        0x00
#define DATA_MODE           0x40

// Register definitions
#define SSD1306_MEMORYMODE 0x20          ///< See datasheet
#define SSD1306_COLUMNADDR 0x21          ///< See datasheet
#define SSD1306_PAGEADDR 0x22            ///< See datasheet
#define SSD1306_SETCONTRAST 0x81         ///< See datasheet
#define SSD1306_CHARGEPUMP 0x8D          ///< See datasheet
#define SSD1306_SEGREMAP 0xA0            ///< See datasheet
#define SSD1306_DISPLAYALLON_RESUME 0xA4 ///< See datasheet
#define SSD1306_NORMALDISPLAY 0xA6       ///< See datasheet
#define SSD1306_INVERTDISPLAY 0xA7       ///< See datasheet
#define SSD1306_SETMULTIPLEX 0xA8        ///< See datasheet
#define SSD1306_DISPLAYOFF 0xAE          ///< See datasheet
#define SSD1306_DISPLAYON 0xAF           ///< See datasheet
#define SSD1306_COMSCANDEC 0xC8          ///< See datasheet
#define SSD1306_SETDISPLAYOFFSET 0xD3    ///< See datasheet
#define SSD1306_SETDISPLAYCLOCKDIV 0xD5  ///< See datasheet
#define SSD1306_SETPRECHARGE 0xD9        ///< See datasheet
#define SSD1306_SETCOMPINS 0xDA          ///< See datasheet
#define SSD1306_SETVCOMDETECT 0xDB       ///< See datasheet

#define SSD1306_SETSTARTLINE 0x40  ///< See datasheet

#define SSD1306_EXTERNALVCC 0x01  ///< External display voltage source
#define SSD1306_SWITCHCAPVCC 0x02 ///< Gen. display voltage from 3.3V

#define SSD1306_RIGHT_HORIZONTAL_SCROLL 0x26              ///< Init rt scroll
#define SSD1306_LEFT_HORIZONTAL_SCROLL 0x27               ///< Init left scroll
#define SSD1306_VERTICAL_AND_RIGHT_HORIZONTAL_SCROLL 0x29 ///< Init diag scroll
#define SSD1306_VERTICAL_AND_LEFT_HORIZONTAL_SCROLL 0x2A  ///< Init diag scroll
#define SSD1306_DEACTIVATE_SCROLL 0x2E                    ///< Stop scroll
#define SSD1306_ACTIVATE_SCROLL 0x2F                      ///< Start scroll
#define SSD1306_SET_VERTICAL_SCROLL_AREA 0xA3             ///< Set scroll range

unsigned char str[] = "Hello world";

// --- [NEW] Bien toan cuc cho ngat ---
static unsigned int irq_number;
static bool invert_mode = false;
static unsigned long last_interrupt_time = 0;
static struct work_struct invert_work; // Dao mau neu nen den

static const unsigned char SSD1306_font[][5]= 
{
    {0x00, 0x00, 0x00, 0x00, 0x00},   // space
    {0x00, 0x00, 0x2f, 0x00, 0x00},   // !
    {0x00, 0x07, 0x00, 0x07, 0x00},   // "
    {0x14, 0x7f, 0x14, 0x7f, 0x14},   // #
    {0x24, 0x2a, 0x7f, 0x2a, 0x12},   // $
    {0x23, 0x13, 0x08, 0x64, 0x62},   // %
    {0x36, 0x49, 0x55, 0x22, 0x50},   // &
    {0x00, 0x05, 0x03, 0x00, 0x00},   // '
    {0x00, 0x1c, 0x22, 0x41, 0x00},   // (
    {0x00, 0x41, 0x22, 0x1c, 0x00},   // )
    {0x14, 0x08, 0x3E, 0x08, 0x14},   // *
    {0x08, 0x08, 0x3E, 0x08, 0x08},   // +
    {0x00, 0x00, 0xA0, 0x60, 0x00},   // ,
    {0x08, 0x08, 0x08, 0x08, 0x08},   // -
    {0x00, 0x60, 0x60, 0x00, 0x00},   // .
    {0x20, 0x10, 0x08, 0x04, 0x02},   // /

    {0x3E, 0x51, 0x49, 0x45, 0x3E},   // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00},   // 1
    {0x42, 0x61, 0x51, 0x49, 0x46},   // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31},   // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10},   // 4
    {0x27, 0x45, 0x45, 0x45, 0x39},   // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30},   // 6
    {0x01, 0x71, 0x09, 0x05, 0x03},   // 7
    {0x36, 0x49, 0x49, 0x49, 0x36},   // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E},   // 9

    {0x00, 0x36, 0x36, 0x00, 0x00},   // :
    {0x00, 0x56, 0x36, 0x00, 0x00},   // ;
    {0x08, 0x14, 0x22, 0x41, 0x00},   // <
    {0x14, 0x14, 0x14, 0x14, 0x14},   // =
    {0x00, 0x41, 0x22, 0x14, 0x08},   // >
    {0x02, 0x01, 0x51, 0x09, 0x06},   // ?
    {0x32, 0x49, 0x59, 0x51, 0x3E},   // @

    {0x7C, 0x12, 0x11, 0x12, 0x7C},   // A
    {0x7F, 0x49, 0x49, 0x49, 0x36},   // B
    {0x3E, 0x41, 0x41, 0x41, 0x22},   // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C},   // D
    {0x7F, 0x49, 0x49, 0x49, 0x41},   // E
    {0x7F, 0x09, 0x09, 0x09, 0x01},   // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A},   // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F},   // H
    {0x00, 0x41, 0x7F, 0x41, 0x00},   // I
    {0x20, 0x40, 0x41, 0x3F, 0x01},   // J
    {0x7F, 0x08, 0x14, 0x22, 0x41},   // K
    {0x7F, 0x40, 0x40, 0x40, 0x40},   // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},   // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F},   // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E},   // O
    {0x7F, 0x09, 0x09, 0x09, 0x06},   // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E},   // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46},   // R
    {0x46, 0x49, 0x49, 0x49, 0x31},   // S
    {0x01, 0x01, 0x7F, 0x01, 0x01},   // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F},   // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F},   // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F},   // W
    {0x63, 0x14, 0x08, 0x14, 0x63},   // X
    {0x07, 0x08, 0x70, 0x08, 0x07},   // Y
    {0x61, 0x51, 0x49, 0x45, 0x43},   // Z

    {0x00, 0x7F, 0x41, 0x41, 0x00},   // [
    {0x55, 0xAA, 0x55, 0xAA, 0x55},   // Backslash (Checker pattern)
    {0x00, 0x41, 0x41, 0x7F, 0x00},   // ]
    {0x04, 0x02, 0x01, 0x02, 0x04},   // ^
    {0x40, 0x40, 0x40, 0x40, 0x40},   // _
    {0x00, 0x03, 0x05, 0x00, 0x00},   // `

    {0x20, 0x54, 0x54, 0x54, 0x78},   // a
    {0x7F, 0x48, 0x44, 0x44, 0x38},   // b
    {0x38, 0x44, 0x44, 0x44, 0x20},   // c
    {0x38, 0x44, 0x44, 0x48, 0x7F},   // d
    {0x38, 0x54, 0x54, 0x54, 0x18},   // e
    {0x08, 0x7E, 0x09, 0x01, 0x02},   // f
    {0x18, 0xA4, 0xA4, 0xA4, 0x7C},   // g
    {0x7F, 0x08, 0x04, 0x04, 0x78},   // h
    {0x00, 0x44, 0x7D, 0x40, 0x00},   // i
    {0x40, 0x80, 0x84, 0x7D, 0x00},   // j
    {0x7F, 0x10, 0x28, 0x44, 0x00},   // k
    {0x00, 0x41, 0x7F, 0x40, 0x00},   // l
    {0x7C, 0x04, 0x18, 0x04, 0x78},   // m
    {0x7C, 0x08, 0x04, 0x04, 0x78},   // n
    {0x38, 0x44, 0x44, 0x44, 0x38},   // o
    {0xFC, 0x24, 0x24, 0x24, 0x18},   // p
    {0x18, 0x24, 0x24, 0x18, 0xFC},   // q
    {0x7C, 0x08, 0x04, 0x04, 0x08},   // r
    {0x48, 0x54, 0x54, 0x54, 0x20},   // s
    {0x04, 0x3F, 0x44, 0x40, 0x20},   // t
    {0x3C, 0x40, 0x40, 0x20, 0x7C},   // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C},   // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C},   // w
    {0x44, 0x28, 0x10, 0x28, 0x44},   // x
    {0x1C, 0xA0, 0xA0, 0xA0, 0x7C},   // y
    {0x44, 0x64, 0x54, 0x4C, 0x44},   // z

    {0x00, 0x10, 0x7C, 0x82, 0x00},   // {
    {0x00, 0x00, 0xFF, 0x00, 0x00},   // |
    {0x00, 0x82, 0x7C, 0x10, 0x00},   // }
    {0x00, 0x06, 0x09, 0x09, 0x06}    // ~ (Degrees)
};

struct ssd1306_dev {
    struct i2c_client *client;         // Pointer to the I2C Client (The Communicator)
    uint8_t frame_buffer[FRAME_BUFFER_SIZE]; // The 1024-byte pixel map 
    struct cdev cdev;
    dev_t dev_num;
};

struct ssd1306_dev *oled_device;

//I2C basic functions
static int I2C_Write(unsigned char *TXBuf, unsigned int TXLen);
static int I2C_Read(unsigned char *RXBuf, unsigned int RXLen);

//OLED functions
static int SSD1306_Write_Command(unsigned int cmd);
static int SSD1306_Write_Data(unsigned char *data, unsigned int dataLen);
static int SSD1306_Init(void);
static void SSD1306_Clear(void);
static void SSD1306_Set_Cursor(uint8_t columnNum, uint8_t pageNum);
static void SSD1306_Write_Char(uint8_t columnNum, uint8_t pageNum,unsigned char c);
static void SSD1306_Write_String(uint8_t columnNum, uint8_t pageNum, unsigned char* str);

//Linux driver interfaces
static int ssd1306_probe(struct i2c_client *client);
static void ssd1306_remove(struct i2c_client *client);
static int __init ssd1306_module_init(void);
static void __exit ssd1306_module_exit(void);

static int ssd1306_open(struct inode *inode, struct file *file);
static int ssd1306_release(struct inode *inode, struct file *file);
static ssize_t ssd1306_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos);

// Moi cho ngat tu day
static void invert_display_work_handler(struct work_struct *work) { // Ham xu ly Bottom Half
    // 1. Đảo trạng thái
    invert_mode = !invert_mode;

    // 2. Gửi lệnh I2C xuống màn hình (Thao tác chậm, được phép ngủ)
    if (invert_mode) {
        SSD1306_Write_Command(SSD1306_INVERTDISPLAY); // 0xA7
    } else {
        SSD1306_Write_Command(SSD1306_NORMALDISPLAY); // 0xA6
    }

    // 3. Chớp đèn LED USR3 (GPIO 56) để báo hiệu
    gpio_set_value(LED_GPIO, 1);
    msleep(100); // Ngủ 100ms - Đây là lý do phải dùng Workqueue thay vì làm trong ngắt
    gpio_set_value(LED_GPIO, 0);

    printk(KERN_INFO "SSD1306: Workqueue executed. Display Mode: %s\n", invert_mode ? "INVERTED" : "NORMAL");
}

static irqreturn_t ssd1306_irq_handler(int irq, void *dev_id) { // Ham xu ly ngat (Top Half)
    unsigned long current_time = jiffies; 

    // Chống rung phím (Debounce) 200ms
    if ((current_time - last_interrupt_time) < msecs_to_jiffies(200)) {
        return IRQ_HANDLED;
    }
    last_interrupt_time = current_time;

    printk(KERN_INFO "SSD1306: Button Pressed (IRQ)! Scheduling Work...\n");

    // Lập lịch cho Bottom Half chạy
    schedule_work(&invert_work);

    return IRQ_HANDLED;
}
//het moi cho ngat

/*I2C basic functions*/
static int I2C_Write(unsigned char *TXBuf, unsigned int TXLen){
    int ret = i2c_master_send(oled_device->client, TXBuf, TXLen);
    return ret;
}

static int I2C_Read(unsigned char *RXBuf, unsigned int RXLen){
    int ret = i2c_master_recv(oled_device->client, RXBuf, RXLen);
    return ret;
}

/*SSD1306 OLED functions*/
static int SSD1306_Write_Command(unsigned int cmd){
    unsigned char TXBuf[2];
    TXBuf[0] = COMMAND_MODE;
    TXBuf[1] = cmd;
    int ret = I2C_Write(TXBuf, 2);
    return ret;
}
static int SSD1306_Write_Data(unsigned char *data, unsigned int dataLen)
{
    unsigned char *TXBuf;
    int ret;

    TXBuf = kmalloc(dataLen + 1, GFP_KERNEL);
    if (!TXBuf)
        return -ENOMEM;

    TXBuf[0] = DATA_MODE;
    memcpy(&TXBuf[1], data, dataLen);

    ret = I2C_Write(TXBuf, dataLen + 1);

    kfree(TXBuf);
    return ret;
}
static int SSD1306_Init(void){ 
    //Turn display off
    SSD1306_Write_Command(SSD1306_DISPLAYOFF);

    //Set display clock divide (P40 datasheet)
    SSD1306_Write_Command(SSD1306_SETDISPLAYCLOCKDIV);
    SSD1306_Write_Command(0x80);

    //Set multiplex ratio (P37 datasheet)
    SSD1306_Write_Command(SSD1306_SETMULTIPLEX);
    SSD1306_Write_Command(0x3F);

    //Set display offset (10.1.15)
    SSD1306_Write_Command(SSD1306_SETDISPLAYOFFSET);
    SSD1306_Write_Command(0x00);

    //Set display start line(10.1.6)
    SSD1306_Write_Command(SSD1306_SETSTARTLINE | 0x00);

    //Charge pump enable
    SSD1306_Write_Command(SSD1306_CHARGEPUMP);
    SSD1306_Write_Command(0x14);
    
    //Set memory addressing mode()
    SSD1306_Write_Command(SSD1306_MEMORYMODE);
    SSD1306_Write_Command(0x00);   

    //Set segment remap()
    SSD1306_Write_Command(SSD1306_SEGREMAP | 0x1);

    //Set COM Output Scan Direction()
    SSD1306_Write_Command(SSD1306_COMSCANDEC);

    //Set COM Pins Hardware Config()
    SSD1306_Write_Command(SSD1306_SETCOMPINS);
    SSD1306_Write_Command(0x12);

    //Set Contrast Control
    SSD1306_Write_Command(SSD1306_SETCONTRAST);
    SSD1306_Write_Command(0xCF);   

    //Set Pre-charge Period
    SSD1306_Write_Command(SSD1306_SETPRECHARGE);
    SSD1306_Write_Command(0xF1);   

    //Set VCOM Deselect Level
    SSD1306_Write_Command(SSD1306_SETVCOMDETECT);
    SSD1306_Write_Command(0x40);   

    //Set Entire Display ON
    SSD1306_Write_Command(SSD1306_DISPLAYALLON_RESUME);

    //Set Normal Display
    SSD1306_Write_Command(SSD1306_NORMALDISPLAY);

    //Display ON
    SSD1306_Write_Command(SSD1306_DISPLAYON);

    return 0;

}
static void SSD1306_Clear(void) {
    memset(oled_device->frame_buffer, 0x00, FRAME_BUFFER_SIZE);
    SSD1306_Set_Cursor(0, 0);
    SSD1306_Write_Data(oled_device->frame_buffer, FRAME_BUFFER_SIZE);
}

static void SSD1306_Set_Cursor(uint8_t columnNum, uint8_t pageNum){
    //Co 128 column va 8 page, 1 column 1 pixel va 1 page 8 pixel
    if ((columnNum < SSD1306_WIDTH ) && (pageNum < SSD1306_PAGES)){ //Cursor co trong gioi han cua man hinh
        SSD1306_Write_Command(SSD1306_COLUMNADDR);
        SSD1306_Write_Command(columnNum);
        SSD1306_Write_Command(SSD1306_WIDTH - 1);

        SSD1306_Write_Command(SSD1306_PAGEADDR);
        SSD1306_Write_Command(pageNum);
        SSD1306_Write_Command(SSD1306_PAGES - 1);
    }
}
static void SSD1306_Write_Char(uint8_t columnNum, uint8_t pageNum,unsigned char c){
    if (c < 32 || c > 126) return;   
    SSD1306_Set_Cursor(columnNum, pageNum);
    const unsigned char *charData = SSD1306_font[c - 32];
    SSD1306_Write_Data((unsigned char *)charData, 5);
    unsigned char space = 0x00;
    SSD1306_Write_Data(&space, 1);
}
static void SSD1306_Write_String(uint8_t columnNum, uint8_t pageNum, unsigned char* str){
    while (*str){
        if (columnNum + 6 > SSD1306_WIDTH)
        {
            columnNum = 0;
            pageNum+= 1;
            if (pageNum >= SSD1306_PAGES) break; 
            SSD1306_Write_Char(columnNum, pageNum, *str);
            str++;
            columnNum += 6;
        }
        else {
            SSD1306_Write_Char(columnNum, pageNum, *str);
            str++;
            columnNum += 6;
        }
        
    }    

}

//Linux driver interfaces
static int ssd1306_probe(struct i2c_client *client)
{
    printk(KERN_INFO DRIVER_NAME ": Probing device at 0x%x\n", client->addr);

    oled_device->client = client;

    SSD1306_Init();
    SSD1306_Clear();
    return 0;
}
static void ssd1306_remove(struct i2c_client *client) {
    printk("Now I am in the Remove function!\n");
}
static const struct of_device_id ssd1306_of_match[] = {
	{ .compatible = "lilac,ssd1306_oled" },
	{ }
};
MODULE_DEVICE_TABLE(of, ssd1306_of_match);

static struct i2c_driver ssd1306_i2c_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = of_match_ptr(ssd1306_of_match),
    },
    .probe = ssd1306_probe,
    .remove = ssd1306_remove,
};


static int ssd1306_open(struct inode *inode, struct file *file) {
    return 0;
}
static int ssd1306_release(struct inode *inode, struct file *file) {
    return 0;
}
static ssize_t ssd1306_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    char *kbuf;
    ssize_t ret;
    int error;

    if (count > SSD1306_WIDTH * SSD1306_PAGES * 6) {
        printk(KERN_WARNING DRIVER_NAME ": Write count too large\n");
        return -EINVAL;
    }

    kbuf = kmalloc(count + 1, GFP_KERNEL);
    if (!kbuf) return -ENOMEM;
    kbuf[count] = '\0';   
    
    SSD1306_Clear();

    // Copy data from userspace
    error = copy_from_user(kbuf, buf, count);
    if (error) {
        kfree(kbuf);
        return -EFAULT;
    }

    SSD1306_Write_String(0, 0, (unsigned char *)kbuf);

   
    kfree(kbuf);

    // Return the number of bytes written
    ret = count;
    *ppos += ret;
    return ret;
}

//ioctl moi
static long ssd1306_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    unsigned char *kbuf;

    switch (cmd) {
        case SSD1306_IOC_CLEAR_SCREEN:
            SSD1306_Clear();
            return 0;

        case SSD1306_IOC_UPDATE_FRAME: //case cho gif
            // Cap phat bo nho 
            kbuf = kmalloc(FRAME_BUFFER_SIZE, GFP_KERNEL);
            if (!kbuf) return -ENOMEM;

            // Copy 1024 bytes tu User Space
            if (copy_from_user(kbuf, (unsigned char __user *)arg, FRAME_BUFFER_SIZE)) {
                kfree(kbuf);
                return -EFAULT;
            }

            // ghi thang xuong I2C
            SSD1306_Set_Cursor(0, 0);
            SSD1306_Write_Data(kbuf, FRAME_BUFFER_SIZE);

            kfree(kbuf);
            return 0;

        default:
            return -ENOTTY;
    }
}


//ioctl cu 
// static long ssd1306_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
// {
//     int contrast;

//     switch (cmd) {
//         case SSD1306_IOC_CLEAR_SCREEN:
//             SSD1306_Clear();
//             return 0;

//         default:
//             return -ENOTTY; // Command not recognized
//     }
// }
struct file_operations ssd1306_fops = {
    .owner = THIS_MODULE,
    .open = ssd1306_open,       
    .release = ssd1306_release, 
	.write = ssd1306_write,
    .unlocked_ioctl = ssd1306_ioctl,
};

//init moi
static int __init ssd1306_module_init(void) {
    int ret;
    oled_device = kzalloc(sizeof(*oled_device), GFP_KERNEL);
    if (!oled_device) return -ENOMEM;

    // 1. Setup Character Device
    if (alloc_chrdev_region(&oled_device->dev_num, 0, 1, DEVICE_NAME)){
        kfree(oled_device); return -1;
    }
    cdev_init(&oled_device->cdev, &ssd1306_fops);
    oled_device->cdev.owner = THIS_MODULE;

    if (cdev_add(&oled_device->cdev, oled_device->dev_num, 1) < 0){
		unregister_chrdev_region(oled_device->dev_num, 1);
        kfree(oled_device); return -1;
    }
    printk(KERN_INFO DRIVER_NAME ": Char Device registered: Major=%d\n", MAJOR(oled_device->dev_num));

    // 2. [NEW] SETUP GPIO CHO BUTTON S2
    if (gpio_request(BTN_GPIO, "ssd1306_btn")) {
        printk(KERN_ERR "Cannot request Button GPIO %d\n", BTN_GPIO);
    } else {
        gpio_direction_input(BTN_GPIO);
        irq_number = gpio_to_irq(BTN_GPIO);
        // Đăng ký ngắt: Bắt sườn xuống (Falling Edge)
        ret = request_irq(irq_number, ssd1306_irq_handler, IRQF_TRIGGER_FALLING, "ssd1306_btn_irq", NULL);
        if (ret) {
            printk(KERN_ERR "Cannot request IRQ %d\n", irq_number);
            gpio_free(BTN_GPIO);
        } else {
            printk(KERN_INFO "SSD1306: Button IRQ registered on GPIO %d\n", BTN_GPIO);
        }
    }

    // 3. [NEW] SETUP GPIO CHO LED USR3
    if (gpio_request(LED_GPIO, "ssd1306_led")) {
        printk(KERN_WARN "Cannot request LED GPIO %d (Make sure you disabled default trigger)\n", LED_GPIO);
    } else {
        gpio_direction_output(LED_GPIO, 0); // Mặc định tắt
    }

    // 4. [NEW] KHỞI TẠO WORKQUEUE
    INIT_WORK(&invert_work, invert_display_work_handler);

    // 5. Setup I2C
    return i2c_add_driver(&ssd1306_i2c_driver);
}
// static int __init ssd1306_module_init(void)
// {
//     oled_device = kzalloc(sizeof(*oled_device), GFP_KERNEL);
//     if (!oled_device)
//         return -ENOMEM;

//     //Alocate major/minor number
//     if (alloc_chrdev_region(&oled_device->dev_num, 0, 1, DEVICE_NAME)){
//         printk(KERN_INFO"Cannot allocate major number");
//         return -1;
//     }

//     //Init cdev structure
//     cdev_init(&oled_device->cdev, &ssd1306_fops);
//     oled_device->cdev.owner = THIS_MODULE;

//     //register the device with the kernel's VFS
//     int status = cdev_add(&oled_device->cdev, oled_device->dev_num, 1);
//     if (status < 0){
//         printk("Error adding cdev\n");
// 		unregister_chrdev_region(oled_device->dev_num, 1);
//         kfree(oled_device);
// 		return status;
//     }

//     printk(KERN_INFO DRIVER_NAME ": Device registered: Major=%d, Minor=%d\n", MAJOR(oled_device->dev_num), MINOR(oled_device->dev_num));

    
//     return i2c_add_driver(&ssd1306_i2c_driver);
// }

static void __exit ssd1306_module_exit(void)
{

    // Them - don dep WORKQUEUE & IRQ
    cancel_work_sync(&invert_work);
    free_irq(irq_number, NULL);
    gpio_free(BTN_GPIO);
    gpio_free(LED_GPIO);

    //Unregister the I2C driver
    i2c_del_driver(&ssd1306_i2c_driver);

    //Delete the cdev
    cdev_del(&oled_device->cdev);
    unregister_chrdev_region(oled_device->dev_num, 1);

    //Free the allocated memory
    kfree(oled_device);
}

// Register the custom init/exit functions
module_init(ssd1306_module_init);
module_exit(ssd1306_module_exit);
