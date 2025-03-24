/* Prototype module for second mandatory DM510 assignment */
#ifndef __KERNEL__
#  define __KERNEL__
#endif
#ifndef MODULE
#  define MODULE
#endif

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>	
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/wait.h>
/* #include <asm/uaccess.h> */
#include <linux/uaccess.h>
#include <linux/semaphore.h>
/* #include <asm/system.h> */
#include <asm/switch_to.h>
// #include <stdio.h>

/* Prototypes - this would normally go in a .h file */

#include "buffer.h"
static int dm510_open( struct inode*, struct file* );
static int dm510_release( struct inode*, struct file* );
static ssize_t dm510_read( struct file*, char*, size_t, loff_t* );
static ssize_t dm510_write( struct file*, const char*, size_t, loff_t* );
long dm510_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

#define DEVICE_NAME "dm510_dev" /* Dev name as it appears in /proc/devices */
#define MAJOR_NUMBER 255
#define MIN_MINOR_NUMBER 0
#define MAX_MINOR_NUMBER 1

#define DEVICE_COUNT 2
#define BUFFER_COUNT 2  // bounded buffer 0 and 1

#define BUFFER_DEFAULT_SIZE 4000
/* end of what really should have been in a .h file */


/*
	STRUCTS
*/
/* file operations struct */
static struct file_operations dm510_fops = 
{
	.owner  		= THIS_MODULE,
	.read    		= dm510_read,
	.write   		= dm510_write,
	.open    		= dm510_open,
	.release 		= dm510_release,
    .unlocked_ioctl = dm510_ioctl
};

struct phat_pipe 
{
	wait_queue_head_t inq, outq;      	/* read and write queues */
	char *buffer, *end;                	/* begin of buf, end of buf */
	int buffersize;                    	/* used in pointer arithmetic */
	// char *rp, *wp;                  	
	int nreaders, nwriters;            	/* number of openings for r/w */
	struct buffer *read_buffer;			/* where to read */
	struct buffer *write_buffer;		/* where to write */
	struct fasync_struct *async_queue; 	/* asynchronous readers */
	struct mutex mutex;                	/* mutual exclusion semaphore */
	struct cdev cdev;                  	/* Char device structure */
};


/*
	Parameters
*/		
static struct phat_pipe devices[DEVICE_COUNT];
static struct buffer buffers[BUFFER_COUNT];
dev_t global_device = MKDEV(MAJOR_NUMBER, MIN_MINOR_NUMBER);
static int max_chefs = 5;

/* no more uppercase */
static int device_count = DEVICE_COUNT; /* number of pipe devices */
static int buffer_count = BUFFER_COUNT;
int buffer_default_size = BUFFER_DEFAULT_SIZE;
char device_name 		= DEVICE_NAME;


/*
 * make a phat pipe
 */
static int setup_cdev(struct phat_pipe *dev, dev_t device)
{
	cdev_init(&dev->cdev, &dm510_fops);
	dev->cdev.owner = THIS_MODULE;
	return cdev_add(&dev->cdev, device, 1);
}


/* called when module is loaded */
int dm510_init_module( void ) 
{
	int i, result;
	result = register_chrdev_region(global_device, device_count, device_name);

	if( result) 
	{
		dprintf("failed to register");
		return result;
	}
	for (i = 0; i < buffer_count; i++) 
	{
		result = buffer_init( buffers+i, BUFFER_DEFAULT_SIZE );
		if( result < 0 )
		{
			dprintf("couldn't allocate memory");
			return result;
		}
	}
	for ( i = 0; i < device_count; i++)
	{
		init_waitqueue_head(&devices[i].inq);
		init_waitqueue_head(&devices[i].outq);
		mutex_init(&devices[i].mutex);
		// dprintf("Device(%d) = (%d, %d)", i, ( i % buffer_count ), 
		// 							(( i + 1 ) % buffer_count )); // maybe this needs to go maybe not
		devices[i].read_buffer = buffers + ( i % buffer_count );
		devices[i].write_buffer = buffers + (( i + 1 ) % buffer_count );
		setup_cdev( devices+i, global_device+i );
	}

	printk(KERN_INFO "DM510: Hello from your device!\n"); // part of original code
	return 0;
}

/* Called when module is unloaded */
void dm510_cleanup_module( void ) 
{
	int i;

	for ( i = 0; i < device_count; i++ ) 
	{
		cdev_del(&devices[i].cdev);
	}
	for ( i = 0; i < buffer_count; i++ )
	{
		buffer_free( buffers + i );
	}
	unregister_chrdev_region(global_device, device_count);

	printk(KERN_INFO "DM510: Module unloaded.\n"); //part of given code
}


/* Called when a process tries to open the device file */
static int dm510_open( struct inode *inode, struct file *filp ) 
{
	struct phat_pipe *dev;

	dev = container_of(inode->i_cdev, struct phat_pipe, cdev);
	filp->private_data = dev;

	if (mutex_lock_interruptible(&dev->mutex)) 
	{
		dprintf("something interrupted the lock");
		return -ERESTARTSYS;
	}

	// checking for too many readers using count and flags
	if (filp->f_mode & FMODE_READ)
	{
		if( dev->nreaders >= max_chefs )
		{
			mutex_unlock(&dev->mutex);
			dprintf("too many cooks in the kictehn (too many readers)");
			return -ERESTARTSYS;
		}
		else
		{
			dev->nreaders++;
		}
	}

	// checking for too many writers using count and flags
	/* use f_mode,not  f_flags: it's cleaner (fs/open.c tells why) */
	if(filp->f_mode & FMODE_WRITE)
	{
		if( dev->nwriters >= 1)
		{
			mutex_unlock(&dev->mutex);
			dprintf("only one writer allowed!");
			return -ERESTARTSYS;
		}
		else
		{
			dev->nwriters++;
		}
	}
	mutex_unlock(&dev->mutex);

	return nonseekable_open(inode, filp);
}


/* Called when a process closes the device file. */
static int dm510_release( struct inode *inode, struct file *filp ) 
{
	struct phat_pipe *dev = filp->private_data;

	mutex_lock(&dev->mutex);
	if (filp->f_mode & FMODE_READ && dev->nreaders)
	{
		dev->nreaders--;
	}
		
	if (filp->f_mode & FMODE_WRITE && dev->nwriters)
	{
		dev->nwriters--;
	}
	if (dev->nreaders + dev->nwriters == 0) 
	{
		kfree(dev->buffer);
		dev->buffer = NULL; /* the other fields are not checked on open */
	}
	mutex_unlock(&dev->mutex);
		
	return 0;
}


/* Called when a process, which already opened the dev file, attempts to read from it. */
static ssize_t dm510_read( struct file *filp,
    char *buf,      /* The buffer to fill with data     */
    size_t count,   /* The max number of bytes to read  */
    loff_t *f_pos )  /* The offset in the file           */
{
	
	struct phat_pipe *dev = filp->private_data;
	char **rp = &dev->read_buffer->rp;		// point to rp in buffer struct
	char **wp = &dev->read_buffer->wp;		// point to wp in buffer struct

	if (mutex_lock_interruptible(&dev->mutex)) 
	{
		dprintf("lock was interrupted ");
		return -ERESTARTSYS;
	}
		
	while (*rp == *wp) /* nothing to read */
	{ 
		mutex_unlock(&dev->mutex); /* release the lock */
		if (filp->f_flags & O_NONBLOCK)
		{
			dprintf("unable to block file pointer");
			return -EAGAIN;
		}

		PDEBUG("\"%s\" reading: going to sleep\n", current->comm);
		if (wait_event_interruptible(dev->inq, (*rp != *wp))) 
		{
			dprintf("reader's sleep interrupted");
			return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
		}
		/* otherwise loop, but first reacquire the lock */
		if (mutex_lock_interruptible(&dev->mutex)) 
		{
			return -ERESTARTSYS;
		}
	}
	/* ok, data is there, return something */
	/* since we use two buffers, we dont check for wrapping like scull did */
	count = buffer_read(dev->read_buffer, buf, count);
	mutex_unlock (&dev->mutex);

	/* finally, awake any writers and return */
	wake_up_interruptible(&dev->outq);
	PDEBUG("\"%s\" did read %li bytes\n",current->comm, (long)count);
	return count; //return number of bytes read
}


/* Called when a process writes to dev file */
static ssize_t dm510_write( struct file *filp,
    const char *buf,/* The buffer to get data from      */
    size_t count,   /* The max number of bytes to write */
    loff_t *f_pos )  /* The offset in the file           */
{
	// char *rp = &dev->read_buffer->rp;
	// char *wp = &dev->read_buffer->wp;

	struct phat_pipe *dev = filp->private_data;

	if (mutex_lock_interruptible(&dev->mutex))
	{
		dprintf("mutex lock interrupted");
		return -ERESTARTSYS;
	}

	/* Make sure there's space to write */
	if(count > buffers->buffersize )
	{
		dprintf("message bigger than buffer");
		return -EMSGSIZE;
	}

	/* ok, space is there, accept something */
	while (space(dev->write_buffer) < count)
	{
		mutex_unlock(&dev->mutex);
		if (filp->f_flags & O_NONBLOCK)
		{
			dprintf("unable to block file pointer");
			return -EAGAIN;
		}

		if (wait_event_interruptible(dev->outq, 
			(space(dev->write_buffer) >= count)))
		{
			dprintf("writer's sleep interrupted ");
			return -EAGAIN;
		}

		if (mutex_lock_interruptible(&dev->mutex))
		{
			dprintf("mutex lock interrupted ");
			return -ERESTARTSYS;
		}
	}
	count = buffer_write(dev->write_buffer, (char*)buf, count);
	PDEBUG("Going to accept %li bytes to %p from %p\n", (long)count, &dev->read_buffer->wp, buf);

	/* finally, awake any reader */
	int i;
	for( i = 0; i < device_count; i++)
	{
		wake_up_interruptible(&devices[i].inq);
	}

	PDEBUG("\"%s\" did write %li bytes\n",current->comm, (long)count);
	mutex_unlock(&dev->mutex);
	return count; //return number of bytes written
}


/* called by system call icotl (I/O control)*/
long dm510_ioctl( 
    struct file *filp, 
    unsigned int cmd,   /* command passed from the user */
    unsigned long arg ) /* argument of the command */
{
	if(cmd == 0)
	{
		return buffers->buffersize;
	}
	else if(cmd == 1)
	{
		for( int i = 0; i < buffer_count; i++ )
		{
			int space_used = buffers[i].buffersize - space(buffers+i);
			if(space_used > arg)
			{
				dprintf("buffer has %lu of used space. Unable to make it %lu", space_used, arg);
				// %lu for unisgned long, we dont know what kind of number it may be.
				return -EINVAL;
			}
			else
			{
				for( int i = 0; i < buffer_count; i++ )
				{
					resize(buffers+i,arg);
				}
			}
		}
	}
	else if(cmd == 2)
	{
		return max_chefs;
	}
	else if(cmd == 3)
	{
		max_chefs = arg;
		//also needs more here but i am tired.
	} 
	// they do more things here but idk what the fck theyre doing.
	printk(KERN_INFO "DM510: ioctl called.\n");
	return 0; //has to be changed
}

module_init( dm510_init_module );
module_exit( dm510_cleanup_module );

MODULE_AUTHOR( "...Sandra K. Johansen and Sofie Løfberg" );
MODULE_LICENSE( "GPL" );
