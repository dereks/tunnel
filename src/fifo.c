/**
 * Copyright (c) 2014 Derek Simkowiak
 */
#include "fifo.h"

#ifndef MAX
  #define MAX(a,b) (((a)>(b)) ? (a): (b))
#endif
#ifndef MIN
  #define MIN(a,b) (((a)<(b)) ? (a): (b))
#endif

FIFO *fifo_new(size_t buffer_size)
{
    FIFO *fifo = (FIFO *)calloc(1, sizeof(FIFO));
    fifo->buffer_size = buffer_size;
    return fifo;
}

void fifo_free(FIFO *fifo)
{
    if (fifo == NULL) { return; }
    free(fifo);
}

void fifo_read(FIFO *fifo, size_t byte_count)
{
    fifo->read_count += byte_count;
}

void fifo_write(FIFO *fifo, size_t byte_count)
{
    fifo->write_count += byte_count;
}

size_t fifo_bytes_free(FIFO *fifo)
{
    return fifo->buffer_size - fifo_bytes_used(fifo);
}

size_t fifo_bytes_used(FIFO *fifo)
{
    return fifo->write_count - fifo->read_count;
}

size_t fifo_read_index(FIFO *fifo)
{
    return fifo->read_count % fifo->buffer_size;
}

size_t fifo_write_size(FIFO *fifo)
{
    // Get the number of bytes to the right of write_index in the
    // entire buffer.  That's the value if the empty space is wrapped.
    // For the case that saved bytes are wrapped, we also clip to
    // the number of bytes free:
    size_t write_size =
        fifo->buffer_size - (fifo->write_count % fifo->buffer_size);
    
    return MIN(write_size, fifo_bytes_free(fifo));
}

size_t fifo_write_index(FIFO *fifo)
{
    return fifo->write_count % fifo->buffer_size;
}

size_t fifo_read_size(FIFO *fifo) {
    size_t read_size =
        fifo->buffer_size - (fifo->read_count % fifo->buffer_size);
    
    return MIN(read_size, fifo_bytes_used(fifo));
}

