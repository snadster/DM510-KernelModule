#ifndef DM510_BUFFER_H
#define DM510_BUFFER_H

#include <linux/slab.h>
#include <linux/errno.h>
// #include <stdio.h>


/*
    this is our header file for the buffer.
    we wanted to make it so it fit with the concept,
    i.e. we have two buffers, but instead of being 
    buffer 0 and 1, they're read and write, since
    each module need to read from and write to
    opposite buffers
*/


struct buffer
{
    char *buffer;                      /* begin of buf, end of buf */
	size_t buffersize;                 /* used in pointer arithmetic might need to use size_t for exlusively positive integers */
	char *rp, *wp; 
    struct mutex mutex; 
};

size_t space(struct buffer *buf) 
{
    if (buf->rp == buf->wp)
    {
        return buf->buffersize -1;
    }
    return ((buf->rp + buf->buffersize - buf->wp) % buf->buffersize) - 1;
}

int resize(struct buffer * buf, size_t size) 
{
    void * pointer;
    mutex_lock(&buf->mutex);

    pointer = kmalloc(size * sizeof(*buf->buffer), GFP_KERNEL);

    if (!pointer) 
    {
        printk(KERN_WARNING "out of memory");
        return -ENOMEM;
    }

    if (buf->wp == buf-> rp) 
    {
        buf->wp = buf->rp = pointer;
    } 
    else if (buf->wp > buf->rp) 
    {
        size = buf->wp - buf->rp;
        memcpy(pointer, buf->rp, size);
        buf->wp = pointer + size;
        buf->rp = pointer;
    } 
    else 
    {
        size = (buf->buffer + buf->buffersize) - buf->rp;
        memcpy(pointer, buf->rp, size);
        buf->rp = pointer;
        memcpy(buf->rp + size, buf->buffer, buf->wp - buf->buffer);
        buf->wp = (buf->rp + size) + (buf->wp - buf->buffer);
    }
    buf->buffer = pointer;
    kfree(buf->buffer);
    mutex_unlock (&buf->mutex);
    return 0;
}

int buffer_init(struct buffer *buf, size_t size)
{
	void *pointer;
	pointer = kmalloc(size * sizeof(*buf->buffer), GFP_KERNEL);

	if(!pointer) 
    {
        printk(KERN_WARNING "out of memory");
        return -ENOMEM; 
    } 

	buf->wp = buf->rp = buf->buffer = pointer;
	buf->buffersize = size;

	return 0;
}

struct buffer *buffer_alloc(size_t size)
{
    struct buffer * buf = kmalloc(sizeof(*buf), GFP_KERNEL);
    buffer_init(buf,size);
    return buf;
}
  
int buffer_free(struct buffer * buf)
{
    kfree(buf->buffer);
    buf->buffer = NULL;
    return 0;
}
  
size_t buffer_write(struct buffer * buf, char * seq, size_t size)
{
    size_t new_size;
    size_t county = 0;
    mutex_lock(&buf->mutex);
    if(buf->wp < buf->rp)
    {
        new_size = min((size_t)(buf->wp - buf->rp) - 1, size);
        int check4 = copy_from_user(buf->wp,seq,new_size);
        if(check4 != 0)
        {
            printk(KERN_WARNING "Bad address at check4");
            return -EFAULT;
        }
        buf->wp += new_size;
        county += new_size;
    }
    else
    {
        // free space after wp
        const size_t a = (buf->buffer + buf->buffersize) - buf->wp;
        // free space from start to rp
        const size_t b = (buf->rp - buf->buffer) % buf->buffersize;
        new_size = min(a,size);
        // printk(KERN_WARNING "a: %lu, b: %lu, size: %lu, ns: %lu, c: %lu", a, b, size, new_size, county);
        int check5 = copy_from_user(buf->wp,seq,new_size);
        if(check5 != 0)
        {
            printk(KERN_WARNING "Bad address at check5");
            return -EFAULT;
        }
        size -= new_size;
        county += new_size;
        if(0 < size)
        {
            new_size = min(b,size);
            buf->wp = buf->buffer;
            // TODO: Possible mistake?
            int check6 = copy_from_user(buf->wp,seq,new_size - 1);
            if(check6 != 0)
            {
                printk(KERN_WARNING "Bad address at check6");
                return -EFAULT;
            }
            county += new_size;
        }
        buf->wp += new_size;
        // printk(KERN_WARNING "a: %lu, b: %lu, size: %lu, ns: %lu, c: %lu", a, b, size, new_size, county);
    }
    // If we reached the end of the buffer, write from the start.
    if (buf->wp == buf->buffer + buf->buffersize) {
        buf->wp = buf->buffer;
    }
    mutex_unlock (&buf->mutex);
    // This might be the wrong count
    return county;
}

size_t buffer_read(struct buffer * buf, char * seq, size_t size)
{
    size_t new_size = 0;
    mutex_lock(&buf->mutex);
    if(buf->rp < buf->wp)
    {
        new_size = min((size_t)(buf->wp - buf->rp), size);
        int check1 = copy_to_user(seq,buf->rp,new_size);
        if(check1 != 0)
        {
            printk(KERN_WARNING "Bad address at check1");
            return -EFAULT;
        }
        buf->rp += new_size;
    } 
    else
    {
        const size_t a = (buf->buffer + buf->buffersize) - buf->rp;
        const size_t b = buf->rp - buf->buffer;
  
        new_size = min(a,size);
        int check2 = copy_to_user(seq,buf->rp,new_size);
        if(check2 != 0)
        {
            printk(KERN_WARNING "Bad address at check2");
            return -EFAULT;
        }
        size -= new_size;
        if(0 < size)
        {
            new_size = min(b,new_size);
            buf->rp = buf->buffer;
            int check3 = copy_to_user(seq,buf->rp,new_size);
            if(check3 != 0)
            {
                printk(KERN_WARNING "Bad address at check3");
                return -EFAULT;
            }
        }
        buf->rp += new_size;
  
    }
    // If we reached the end of the buffer, read from the start.
    if (buf->rp == buf->buffer + buf->buffersize) {
        buf->rp = buf->buffer;
    }
    mutex_unlock (&buf->mutex);
    return new_size;
}
#endif