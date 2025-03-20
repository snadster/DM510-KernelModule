#ifndef DM510_BUFFER_H
#define DM510_BUFFER_H

#include <linux/slab.h>
#include <linux/errno.h>

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
}

size_t space(struct buffer * buf) 
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
        printf("out of memory");
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

int buffer_init(struct buffer * buf, size_t size)
{
	void * pointer;
	pointer = kmalloc(size * sizeof(*head->buffer),GFP_KERNEL);

	if(!pointer) 
    {
        printf("out of memory");
        return -ENOMEM; 
    } 

	buf->wp = buf->rp = buf->buffer = pointer;
	buf->size = size;

	return 0;
}

struct buffer * buffer_alloc(size_t size)
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
    int new_size;
    mutex_lock(&buf->mutex);
    if(buf->wp < buf->rp)
    {
        new_size = min((size_t)(buf->wp - buf->rp) - 1, size);
        copy_from_user(buf->wp,seq,new_size);
        buf->wp += new_size;
    }
    else
    {
        const size_t a = (buf->buffer + buf->size) - buf->wp;
        const size_t b = (buf->rp - buf->buffer) % buf->size;
        new_size = min(a,size);
        copy_from_user(buf->wp,seq,new_size);
        size -= new_size;
        if(0 < size)
        {
            new_size = min(b,size);
            buf->wp = buf->buffer;
            copy_from_user(buf->wp,seq,new_size - 1);
        }
        buf->wp += new_size;
    }
    mutex_unlock (&buf->mutex);
    return new_size;
}

size_t buffer_read(struct buffer * buf, char * seq, size_t size)
{
    int new_size = 0;
    mutex_lock(&buf->mutex);
    if(buf->rp < buf->wp)
    {
        new_size = min((size_t)(buf->wp - buf->rp), size);
        copy_to_user(seq,buf->rp,new_size);
        buf->rp += new_size;
    } 
    else
    {
        const size_t a = (buf->buffer + buf->size) - buf->rp;
        const size_t b = buf->rp - buf->buffer;
  
        new_size = min(a,size);
        copy_to_user(seq,buf->rp,new_size);
        size -= new_size;
        if(0 < size)
        {
            new_size = min(b,new_size);
            buf->rp = buf->buffer;
            copy_to_user(seq,buf->rp,new_size);
        }
        buf->rp += new_size;
  
    }
    mutex_unlock (&buf->mutex);
    return new_size;
}
#endif