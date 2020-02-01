#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h> // printk()
#include <linux/slab.h> // kmalloc()
#include <linux/fs.h> // everything...
#include <linux/errno.h> // error codes
#include <linux/types.h> // size_t
#include <linux/proc_fs.h>
#include <linux/fcntl.h> // O_ACCMODE
#include <linux/seq_file.h>
#include <linux/cdev.h>
#include <linux/random.h> // get_random_bytes()

#include <asm/switch_to.h> // cli(), *_flags
#include <asm/uaccess.h> // copy_*_user


// check if linux/uaccess.h is required for copy_*_user
// instead of asm/uaccess
// required after linux kernel 4.1+ ?
#ifndef __ASM_ASM_UACCESS_H
    #include <linux/uaccess.h>
#endif


#include "mastermind_ioctl.h"

#define MASTERMIND_MAJOR 0
#define MASTERMIND_NR_DEVS 1
#define MASTERMIND_MAX_GUESSES 10
#define MASTERMIND_MAX_LINES 256
#define MASTERMIND_LINE_LENGTH 16
#define MASTERMIND_GUESS_LENGTH 4

int mastermind_major = MASTERMIND_MAJOR;
int mastermind_minor = 0;
int mastermind_nr_devs = MASTERMIND_NR_DEVS;
int mastermind_max_guesses = MASTERMIND_MAX_GUESSES;
char* mastermind_number = NULL;

module_param(mastermind_nr_devs, int, S_IRUGO);
module_param(mastermind_major, int, S_IRUGO);
module_param(mastermind_max_guesses, int, S_IRUGO);
module_param(mastermind_number, charp, S_IRUGO);

MODULE_AUTHOR("Ömer Faruk ABACI, Damla Nisa ÇEVİK, Muhammed Enes KUYCAK");
MODULE_LICENSE("Dual BSD/GPL");

struct mastermind_dev {
    char** data;
    char* guess;
    char* guess_count_str;
    int guess_count;
    struct semaphore sem;
    struct cdev cdev;
}; 

struct mastermind_dev *mastermind_devices;


int mastermind_trim(struct mastermind_dev *dev)
{
    int i;
    if (dev->data) {
        for (i = 0; i < MASTERMIND_MAX_LINES; i++) {
            if (dev->data[i]){
                kfree(dev->data[i]);
			}
        }
        kfree(dev->data);
    }
    
    if (dev->guess)
        kfree(dev->guess);
    
	if (dev->guess_count_str)
		kfree(dev->guess_count_str);

    dev->guess_count = 0;
	dev->guess_count_str = NULL;
    dev->guess = NULL;
    dev->data = NULL;
    
    return 0;
}


int mastermind_open(struct inode *inode, struct file *filp)
{
    struct mastermind_dev *dev;
    dev = container_of(inode->i_cdev, struct mastermind_dev, cdev);
    filp->private_data = dev;

    return 0;
}


int mastermind_release(struct inode *inode, struct file *filp)
{
    return 0;
}


ssize_t mastermind_read(struct file *filp, char __user *buf, size_t count,
                   loff_t *f_pos)
{
    struct mastermind_dev *dev = filp->private_data;
    ssize_t retval = 0;
	int line;

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
        
    if (count > MASTERMIND_LINE_LENGTH)
		count = MASTERMIND_LINE_LENGTH;
		
	if(*f_pos >= dev->guess_count * MASTERMIND_LINE_LENGTH){
		retval = 0;
		goto out;
	}

	if(*f_pos + count > dev->guess_count * MASTERMIND_LINE_LENGTH)
		count = dev->guess_count * MASTERMIND_LINE_LENGTH - *f_pos;
		
	line = *f_pos / MASTERMIND_LINE_LENGTH;

	if(line < (*f_pos + count - 1) / MASTERMIND_LINE_LENGTH){
		count = MASTERMIND_LINE_LENGTH - *f_pos % MASTERMIND_LINE_LENGTH;
	}
	
    if (dev->data == NULL || !dev->data[line])
		goto out;
        
	if (copy_to_user(buf, dev->data[line] + (*f_pos % MASTERMIND_LINE_LENGTH), count)) {
		retval = -EFAULT;
		goto out;
	}
	
	*f_pos += count;
	retval = count;

  out:
    up(&dev->sem);
    return retval;
}


ssize_t mastermind_write(struct file *filp, const char __user *buf, size_t count,
                    loff_t *f_pos)
{
    struct mastermind_dev *dev = filp->private_data;
    ssize_t retval = -ENOMEM;
    int i, j, in_place_counter, out_of_place_counter;
    char guess_temp[4], mastermind_temp[4];

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
        
    if(!mastermind_number){
        int number;
        mastermind_number = kmalloc(5 * sizeof(char), GFP_KERNEL);
        
		for(i = 0; i < 4; i++){
			number = get_random_int();
			if (number < 0)
				number = -number;
			number %= 10;
			mastermind_number[i] = '0' + number;
		}
		mastermind_number[4] = '\0';
	}
    
    if(!dev->guess) {
		dev->guess = kmalloc((MASTERMIND_GUESS_LENGTH + 1) * sizeof(char), GFP_KERNEL);
        if (!dev->guess)
            goto out;
	}
    
    if(!dev->guess_count_str) {
		dev->guess_count_str = kmalloc(4 * sizeof(char), GFP_KERNEL);
		if (!dev->guess_count_str)
            goto out;
		strcpy(dev->guess_count_str, "0000");
	}
        
    if (dev->guess_count == mastermind_max_guesses){
		printk(KERN_ERR "Maximum guesses are exceeded. Mastermind number was: %s.\n", mastermind_number);
        retval = -EPERM;
        goto out;
	}
	
	if (count != MASTERMIND_GUESS_LENGTH + 1){
		printk(KERN_ERR "Invalid guess length %d. Guess' length must be exactly 4.\n", count);
        retval = -EINVAL;
        goto out;
	}

    if (!dev->data) {
        dev->data = kmalloc(MASTERMIND_MAX_LINES * sizeof(char *), GFP_KERNEL);
        if (!dev->data)
            goto out;
        memset(dev->data, 0, MASTERMIND_MAX_LINES * sizeof(char *));
    }

    if (!dev->data[dev->guess_count]) {
        dev->data[dev->guess_count] = kmalloc(MASTERMIND_LINE_LENGTH * sizeof(char), GFP_KERNEL);
        if (!dev->data[dev->guess_count])
            goto out;
    }

    if (copy_from_user(dev->guess, buf, count)) {
        retval = -EFAULT;
        goto out;
    }
    
    dev->guess[count - 1] = '\0';

    for(i = 3; i >= 0; i--){
		dev->guess_count_str[i]++;
		if(dev->guess_count_str[i] > '9')
			dev->guess_count_str[i] = '0';
		else
			break;
	}
	
	in_place_counter = 0;
	out_of_place_counter = 0;
	
	for(i = 0; i < MASTERMIND_GUESS_LENGTH; i++){
		guess_temp[i] = dev->guess[i];
		mastermind_temp[i] = mastermind_number[i];
	}
	
	for(i = 0; i < MASTERMIND_GUESS_LENGTH; i++){
		if(guess_temp[i] == mastermind_temp[i]){
			in_place_counter++;
			guess_temp[i] = '+';
			mastermind_temp[i] = '+';
		}
	}
	
	for(i = 0; i < MASTERMIND_GUESS_LENGTH; i++){
		if (guess_temp[i] == '+')
			continue;
		for(j = 0; j < MASTERMIND_GUESS_LENGTH; j++){
			if (mastermind_temp[j] == '+')
				continue;
			
			if (guess_temp[i] == mastermind_temp[j]){
				out_of_place_counter++;
				guess_temp[i] = '+';
				mastermind_temp[j] = '+'; 
			}		
		}
	}
	
	strcpy(dev->data[dev->guess_count], dev->guess);
	dev->data[dev->guess_count][4] = ' ';
	dev->data[dev->guess_count][5] = '0' + in_place_counter;
	dev->data[dev->guess_count][6] = '+';
	dev->data[dev->guess_count][7] = ' ';
	dev->data[dev->guess_count][8] = '0' + out_of_place_counter;
	dev->data[dev->guess_count][9] = '-';
	dev->data[dev->guess_count][10] = ' ';
	strncpy(dev->data[dev->guess_count] + 11, dev->guess_count_str, 4);
	dev->data[dev->guess_count][15] = '\n';

	dev->guess_count++;
	
	if (in_place_counter == MASTERMIND_GUESS_LENGTH){
		printk(KERN_INFO "You have won after %d guesses! Mastermind number was: %s\n", dev->guess_count, mastermind_number);
	}
	
    retval = count;

  out:
	*f_pos = 0;
    up(&dev->sem);
    return retval;
}

long mastermind_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct mastermind_dev *dev = filp->private_data;
	int retval = 0, err = 0, i;
	
	if (_IOC_TYPE(cmd) != MASTERMIND_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > MASTERMIND_IOC_MAXNR) return -ENOTTY;
	
	
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err =  !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	if (err) return -EFAULT;
	
	
	switch(cmd) {
		case MASTERMIND_REMAINING:
			retval = mastermind_max_guesses - dev->guess_count;
			break;

		case MASTERMIND_ENDGAME:
			mastermind_trim(dev);
			break;

		case MASTERMIND_NEWGAME:
			mastermind_trim(dev);

			if(!mastermind_number)
				mastermind_number = kmalloc(5 * sizeof(char), GFP_KERNEL);
			
			for(i = 0; i < MASTERMIND_GUESS_LENGTH; i++){
				mastermind_number[i] = *((char*) arg + i);
			}
			break;
			
		default:
			return -ENOTTY;
	
	}

	return retval;
}


loff_t mastermind_llseek(struct file *filp, loff_t off, int whence)
{
	return -ESPIPE;
}


struct file_operations mastermind_fops = {
    .owner =    THIS_MODULE,
    .llseek =   mastermind_llseek,
    .read =     mastermind_read,
    .write =    mastermind_write,
    .unlocked_ioctl =  mastermind_ioctl,
    .open =     mastermind_open,
    .release =  mastermind_release,
};


void mastermind_cleanup_module(void)
{
    int i;
    dev_t devno = MKDEV(mastermind_major, mastermind_minor);

    if (mastermind_devices) {
        for (i = 0; i < mastermind_nr_devs; i++) {
            mastermind_trim(mastermind_devices + i);
            cdev_del(&mastermind_devices[i].cdev);
        }
    kfree(mastermind_devices);
    kfree(mastermind_number);
    }

    unregister_chrdev_region(devno, mastermind_nr_devs);
}


int mastermind_init_module(void)
{
    int result, i;
    int err;
    dev_t devno = 0;
    struct mastermind_dev *dev;

    if (mastermind_major) {
        devno = MKDEV(mastermind_major, mastermind_minor);
        result = register_chrdev_region(devno, mastermind_nr_devs, "mastermind");
    } else {
        result = alloc_chrdev_region(&devno, mastermind_minor, mastermind_nr_devs,
                                     "mastermind");
        mastermind_major = MAJOR(devno);
    }
    if (result < 0) {
        printk(KERN_WARNING "mastermind: can't get major %d\n", mastermind_major);
        return result;
    }

    mastermind_devices = kmalloc(mastermind_nr_devs * sizeof(struct mastermind_dev),
                            GFP_KERNEL);
    if (!mastermind_devices) {
        result = -ENOMEM;
        goto fail;
    }
    memset(mastermind_devices, 0, mastermind_nr_devs * sizeof(struct mastermind_dev));

    /* Initialize each device. */
    for (i = 0; i < mastermind_nr_devs; i++) {
        dev = &mastermind_devices[i];
        dev->guess = NULL;
        dev->guess_count_str = NULL;
        dev->guess_count = 0;
        sema_init(&dev->sem,1);
        devno = MKDEV(mastermind_major, mastermind_minor + i);
        cdev_init(&dev->cdev, &mastermind_fops);
        //dev->cdev.owner = THIS_MODULE;
        //dev->cdev.ops = &mastermind_fops;
        err = cdev_add(&dev->cdev, devno, 1);
        if (err){
            printk(KERN_NOTICE "Error %d adding mastermind%d\n", err, i);
            //result = err;
            //goto fail;
            //Is it because cdev_add should be run for each device?
		}
    }
    
	if (mastermind_number) {
		if(strlen(mastermind_number) != 4){
			printk(KERN_ERR "\"mastermind_number\"'s length must be exactly 4.\n");
			result = -EINVAL;
			goto fail;
		}
		
		for(i = 0; i < 4; i++){
			if(mastermind_number[i] < '0' || mastermind_number[i] > '9'){
				printk(KERN_ERR "\"mastermind_number\" must include digit characters only.\n");
				result = -EINVAL;
				goto fail;
			}
		}
		
		printk(KERN_INFO "\"mastermind_number\" = %s\n", mastermind_number);
	}
	
	if (mastermind_max_guesses > 256){
		printk(KERN_ERR "\"mastermind_max_guesses\" must be <= 256.\n");
		result = -EINVAL;
		goto fail;
	}
	
	printk(KERN_INFO "\"mastermind_max_guesses\" = %d\n", mastermind_max_guesses);
	
    return 0;

  fail:
    mastermind_cleanup_module();
    return result;
}

module_init(mastermind_init_module);
module_exit(mastermind_cleanup_module);
